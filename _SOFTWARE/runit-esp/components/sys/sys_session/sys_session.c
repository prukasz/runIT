#include "sys_session.h"

#include <string.h>

#include "board_time.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "sys_session_call.h"
#include "sys_session_hello.h"
#include "sys_session_internal.h"
#include "utils.h"

#define TICK_MS         10u   // the timer: pings, session timeouts, call timeouts
#define REJECT_BYTES    64u   // an error record answered on the RX task (its stack)
#define NVS_NAMESPACE   "session"
#define NVS_KEY_ID      "id"
#define CONTROL_DATA_MAX ((uint32_t)sizeof(sys_session_hello_t))  // the largest control frame data: the hello

_Static_assert(CONFIG_SYS_SESSION_TIMEOUT_MS >= SYS_SESSION_TIMEOUT_MIN_MS, "CONFIG_SYS_SESSION_TIMEOUT_MS too short");
_Static_assert((ROUTER_FLAG_URGENT | ROUTER_FLAG_NOREPLY) == SYS_SESSION_FRAME_FLAGS, "the frame byte's flags");
_Static_assert(ROUTER_STATUS_ERROR == 1, "the frame byte's status bit");
_Static_assert(ROUTER_SRC_SELF == SYS_SESSION_ID_SELF, "the board's own commands");
_Static_assert(ROUTER_SRC_NONE == SYS_SESSION_ID_NONE, "a command with no peer");
_Static_assert(SYS_SESSION_TIMEOUT_MAX_MS + TICK_MS <= UINT16_MAX, "a session's silence and ping in 16 bits");

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static struct {
  session_slot_t sessions[CONFIG_SYS_SESSION_MAX];
  uint8_t board_id;
  int32_t master;       // the master's session (a slave's only one), -1: none, this board is the master
  uint32_t timeout_ms;  // this board's timeout of its master's and its clients' sessions (the dead-man's time)
  esp_timer_handle_t timer;
  sys_session_gone_f gone[CONFIG_SYS_SESSION_PEER_LISTENERS];
  sys_session_reached_f reached[CONFIG_SYS_SESSION_PEER_LISTENERS];
  sys_session_stream_f stream[CONFIG_SYS_SESSION_STREAM_LISTENERS];
} s_state = {.master = -1, .timeout_ms = CONFIG_SYS_SESSION_TIMEOUT_MS};

/* ----------------------------------------------------------------------------
 * The sessions: lookups, the way to a peer, copies
 * ------------------------------------------------------------------------- */

/* The index in s_state.sessions of the session that runs over `provider` (the provider -> session lookup), -1 when
 * no session is attached to it. Under the lock. */
static int32_t session_find_by_provider(sys_data_provider_id_t provider) {
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_slot_t* s = &s_state.sessions[i];
    if (session_in_use(s) && sys_data_provider_same_id(s->provider, provider)) {
      return i;
    }
  }
  return -1;
}

/* The index of the open session whose peer has this board or client id (a slave, its master or a client), -1 when
 * there is none. Under the lock. */
static int32_t session_find_by_peer_id(uint8_t peer_id) {
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_slot_t* s = &s_state.sessions[i];
    if (session_is_open(s) && s->hello.id == peer_id) {
      return i;
    }
  }
  return -1;
}

/* The session a frame for `dst` leaves on, -1: no way. A slave reaches everything through its master (while that
 * session is open); the master through the open session of that slave or client. Under the lock. */
static int32_t session_route(uint8_t dst) {
  if (s_state.master >= 0) {
    return session_is_open(&s_state.sessions[s_state.master]) ? s_state.master : -1;
  }
  return session_find_by_peer_id(dst);
}

int32_t sys_session_route(uint8_t dst) {
  portENTER_CRITICAL(&s_lock);
  const int32_t at = session_route(dst);
  portEXIT_CRITICAL(&s_lock);
  return at;
}

bool sys_session_wired(uint8_t peer) {
  portENTER_CRITICAL(&s_lock);
  const int32_t at = session_find_by_peer_id(peer);
  const sys_data_provider_id_t provider = at >= 0 ? s_state.sessions[at].provider : (sys_data_provider_id_t){0};
  portEXIT_CRITICAL(&s_lock);
  return at >= 0 && sys_data_provider_is_wired(provider);
}

// This board's timeout of a session of this kind.
static uint32_t timeout_of(uint8_t peer) {
  return peer == SYS_SESSION_SLAVE ? CONFIG_SYS_SESSION_TIMEOUT_MS : s_state.timeout_ms;
}

// No lock: a session's kind is one byte, set by attach and kept while the slot is in use.
uint32_t sys_session_timeout(int32_t at) {
  return timeout_of(s_state.sessions[at].peer);
}

session_slot_t sys_session_slot(int32_t at) {
  portENTER_CRITICAL(&s_lock);
  const session_slot_t slot = s_state.sessions[at];
  portEXIT_CRITICAL(&s_lock);
  return slot;
}

uint8_t sys_session_list(sys_session_info_t out[CONFIG_SYS_SESSION_MAX]) {
  uint8_t count = 0;
  portENTER_CRITICAL(&s_lock);
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_slot_t* s = &s_state.sessions[i];
    if (session_in_use(s)) {
      out[count++] = (sys_session_info_t){
          .provider = s->provider,
          .peer = s->peer,
          .open = session_is_open(s),
          .peer_id = s->hello.id,
          .timeout_ms = (uint16_t)timeout_of(s->peer),
      };
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return count;
}

bool sys_session_open(int32_t at, const sys_session_hello_state_t* peer, bool* was_open) {
  bool taken = false;
  portENTER_CRITICAL(&s_lock);
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_slot_t* o = &s_state.sessions[i];
    taken |= i != at && session_is_open(o) && o->hello.id == peer->id;
  }
  session_slot_t* s = &s_state.sessions[at];
  *was_open = session_is_open(s);
  if (!taken) {
    s->hello = *peer;
    s->silent_ms = 0;
  }
  portEXIT_CRITICAL(&s_lock);
  return !taken;
}

/* ----------------------------------------------------------------------------
 * Frames out
 * ------------------------------------------------------------------------- */

// A whole frame on session `at`'s provider.
err_h sys_session_send_raw(int32_t at, const uint8_t* frame, uint32_t len, uint8_t flags) {
  portENTER_CRITICAL(&s_lock);
  const sys_data_provider_id_t provider = s_state.sessions[at].provider;
  portEXIT_CRITICAL(&s_lock);
  return sys_data_provider_send(provider, frame, len, flags);
}

// One frame on a session's provider: the header goes into the HDR bytes free in front of `body`.
static err_h send_frame(int32_t at, const sys_session_hdr_t* hdr, uint8_t* body, uint32_t len, uint8_t flags) {
  memcpy(body - HDR, hdr, HDR);
  return sys_session_send_raw(at, body - HDR, HDR + len, flags);
}

err_h sys_session_send_control(int32_t at, uint8_t op, const uint8_t* data, uint32_t len) {
  uint8_t buf[HDR + 1u + CONTROL_DATA_MAX];
  ERR_CHECK(len <= CONTROL_DATA_MAX, ERR_CMD_BAD_LEN, .got = (uint16_t)len, .need = CONTROL_DATA_MAX);
  portENTER_CRITICAL(&s_lock);
  const uint8_t dst = s_state.sessions[at].hello.id;
  portEXIT_CRITICAL(&s_lock);
  const sys_session_hdr_t hdr = {.frame = FRAME(SYS_SESSION_CONTROL), .dst = dst, .src = s_state.board_id};
  buf[HDR] = op;
  if (len > 0) {
    memcpy(buf + HDR + 1, data, len);
  }
  return send_frame(at, &hdr, buf + HDR, 1u + len, SYS_DATA_PROVIDER_SEND_NOWAIT);
}

err_h sys_session_stream(uint8_t peer, uint16_t seq, const uint8_t* data, uint32_t len, uint8_t flags) {
  ERR_CHECK(data != NULL && len > 0 && len <= CMD_DATA_MAX, ERR_CMD_BAD_ARG);
  const int32_t at = sys_session_route(peer);
  ERR_CHECK(at >= 0, ERR_SESSION_NO_ROUTE, .dst = peer);
  uint8_t buf[CMD_FRAME_MAX];
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_STREAM) | (flags & SYS_SESSION_FRAME_FLAGS)),
      .dst = peer,
      .src = s_state.board_id,
      .seq = seq,
  };
  memcpy(buf + HDR, data, len);
  return send_frame(at, &hdr, buf + HDR, len, sys_session_send_flags(flags));
}

// A frame to every open slave; the first error comes back, the rest are still sent.
static err_h send_down(const uint8_t* frame, uint32_t len, uint8_t flags) {
  err_h first = NULL;
  for (int32_t at = 0; at < CONFIG_SYS_SESSION_MAX; at++) {
    portENTER_CRITICAL(&s_lock);
    const session_slot_t* s = &s_state.sessions[at];
    const bool slave = session_is_open(s) && s->peer == SYS_SESSION_SLAVE;
    portEXIT_CRITICAL(&s_lock);
    if (slave) {
      ERR_KEEP_FIRST(first, sys_session_send_raw(at, frame, len, sys_session_send_flags(flags)));
    }
  }
  return first;
}

err_h sys_session_send_broadcast(const uint8_t* frame, uint32_t len, uint8_t flags) {
  if (s_state.master < 0) {
    return send_down(frame, len, flags);
  }
  const int32_t up = sys_session_route(SYS_SESSION_ID_BROADCAST);
  return up >= 0 ? sys_session_send_raw(up, frame, len, sys_session_send_flags(flags)) : NULL;
}

// GONE to every slave (dst 0xFF): a client of the master can no longer be reached, so what it has on them ends.
static void send_gone(uint8_t client) {
  const sys_session_hdr_t hdr = {.frame = FRAME(SYS_SESSION_CONTROL), .dst = SYS_SESSION_ID_BROADCAST,
                                 .src = s_state.board_id};
  uint8_t frame[HDR + 2u];
  memcpy(frame, &hdr, HDR);
  frame[HDR] = SYS_SESSION_GONE;
  frame[HDR + 1] = client;
  err_report(send_down(frame, sizeof(frame), 0));
}

/* ----------------------------------------------------------------------------
 * Listeners
 * ------------------------------------------------------------------------- */

// The body of an on_* call: `fn` into the first free slot of a listener table (again: one slot).
#define ADD_LISTENER(table, fn)                     \
  ERR_CHECK((fn) != NULL, ERR_CMD_BAD_ARG);         \
  for (uint32_t i = 0; i < ARRAY_LEN(table); i++) { \
    if ((table)[i] == NULL || (table)[i] == (fn)) { \
      (table)[i] = (fn);                            \
      return NULL;                                  \
    }                                               \
  }                                                 \
  return ERR_NEW(ERR_SESSION_FULL)

err_h sys_session_on_gone(sys_session_gone_f fn) {
  ADD_LISTENER(s_state.gone, fn);
}

err_h sys_session_on_reached(sys_session_reached_f fn) {
  ADD_LISTENER(s_state.reached, fn);
}

err_h sys_session_on_stream(sys_session_stream_f fn) {
  ADD_LISTENER(s_state.stream, fn);
}

static void tell_gone(uint8_t peer, uint8_t via) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_state.gone) && s_state.gone[i] != NULL; i++) {
    s_state.gone[i](peer, via);
  }
}

void sys_session_reached(uint8_t peer) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_state.reached) && s_state.reached[i] != NULL; i++) {
    s_state.reached[i](peer);
  }
}

static void tell_stream(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_state.stream) && s_state.stream[i] != NULL; i++) {
    s_state.stream[i](hdr->src, hdr->seq, body, len);
  }
}

/* ----------------------------------------------------------------------------
 * Ending a session
 * ------------------------------------------------------------------------- */

// Ends a session: its peer is gone.
void sys_session_end(int32_t at) {
  portENTER_CRITICAL(&s_lock);
  session_slot_t* s = &s_state.sessions[at];
  const bool was_open = session_is_open(s);
  const uint8_t peer = s->hello.id;
  const uint8_t via = s->peer;
  s->hello.id = SYS_SESSION_ID_NONE;
  portEXIT_CRITICAL(&s_lock);
  if (!was_open) {
    return;
  }
  sys_session_call_fail_via(at);
  if (via == SYS_SESSION_MASTER) {
    tell_gone(SYS_SESSION_ID_BROADCAST, via);  // every peer: a slave reaches all of them through its master
    return;
  }
  tell_gone(peer, via);
  if (via == SYS_SESSION_CLIENT) {
    send_gone(peer);  // its subscriptions and watches on the slaves end too; a slave has nothing of another slave
  }
}

/* ----------------------------------------------------------------------------
 * Answers
 * ------------------------------------------------------------------------- */

/* The answer to a command, to its source: its header goes into the HDR bytes in front of `data`. It is the router's
 * `done` (ctx NULL: it waits for room in the provider's queue at most the driver's bound), and the RX task's answer
 * to a request it does not run (ctx: the provider's send flags, NOWAIT). */
static err_h session_answer(void* ctx, const cmd_req_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  const int32_t at = sys_session_route(cmd->src);
  ERR_CHECK(at >= 0, ERR_SESSION_NO_ROUTE, .dst = cmd->src);
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_ANSWER) | (status == ROUTER_STATUS_ERROR ? SYS_SESSION_FRAME_STATUS : 0)),
      .dst = cmd->src,
      .src = s_state.board_id,
      .seq = cmd->seq,
  };
  const uint8_t urgent = (cmd->flags & ROUTER_FLAG_URGENT) ? SYS_DATA_PROVIDER_SEND_URGENT : 0;
  const uint8_t flags = (uint8_t)((uintptr_t)ctx | urgent);
  return send_frame(at, &hdr, data, len, flags);
}

// A request this board does not run (refused, no route): its error record to its source, at once, never waiting.
static void answer_error(const cmd_req_t* cmd, err_h err) {
  uint8_t buf[HDR + REJECT_BYTES];
  const uint32_t n = cmd_err_record(cmd, err, buf + HDR, REJECT_BYTES);
  void* const nowait = (void*)(uintptr_t)SYS_DATA_PROVIDER_SEND_NOWAIT;
  err_report(session_answer(nowait, cmd, ROUTER_STATUS_ERROR, buf + HDR, n));
}

/* ----------------------------------------------------------------------------
 * What comes in (the driver's RX task)
 * ------------------------------------------------------------------------- */

// The command of a request frame, for the router or for its error record.
static cmd_req_t request_cmd(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  return (cmd_req_t){.req = body, .req_len = len, .seq = hdr->seq, .src = hdr->src,
                     .flags = (uint8_t)(hdr->frame & SYS_SESSION_FRAME_FLAGS)};
}

// GONE from the master: this client of it can no longer be reached.
static void on_gone_frame(int32_t at, const uint8_t* body, uint32_t len) {
  if (at == s_state.master && len >= 1u) {
    tell_gone(body[0], SYS_SESSION_VIA_NOTICE);
  }
}

static void on_control(int32_t at, const uint8_t* body, uint32_t len) {
  if (len == 0) {
    return;
  }
  switch (body[0]) {
    case SYS_SESSION_HELLO:
    case SYS_SESSION_NO_SESSION:
      sys_session_hello_control(at, body, len);
      break;
    case SYS_SESSION_GONE:
      on_gone_frame(at, body + 1, len - 1);
      break;
    default:  // PING: its arrival is all it says
      break;
  }
}

// A request for this board (or a broadcast): queued to the router, which runs it later on its task and gives the answer
// to session_answer. A request the router does not take is answered and reported here.
static void pass_to_router(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  cmd_req_t cmd = request_cmd(hdr, body, len);
  if (hdr->dst == SYS_SESSION_ID_BROADCAST) {
    cmd.flags |= ROUTER_FLAG_NOREPLY;  // a broadcast is never answered
  }
  err_h err = router_submit(&cmd, session_answer, NULL);  // copied; never blocks
  if (err == NULL) {
    return;
  }
  if (!(cmd.flags & ROUTER_FLAG_NOREPLY)) {
    answer_error(&cmd, err);
  }
  router_report_cmd(&cmd, err);
}

/* A frame for another board or client, which only the master relays (between a client and a slave): on, to the route's
 * provider with NOWAIT, without the router and without keeping anything. A slave relays nothing, and the master never
 * from a slave to a slave. A request that can't go on is answered to its source; an answer or a stream frame is
 * dropped. */
static void forward(int32_t from, const sys_session_hdr_t* hdr, const uint8_t* data, uint32_t len) {
  portENTER_CRITICAL(&s_lock);
  int32_t to = from == s_state.master ? -1 : session_route(hdr->dst);
  if (to >= 0 && s_state.sessions[from].peer == SYS_SESSION_SLAVE && s_state.sessions[to].peer == SYS_SESSION_SLAVE) {
    to = -1;
  }
  portEXIT_CRITICAL(&s_lock);
  err_h err = to < 0 ? ERR_NEW(ERR_SESSION_NO_ROUTE, .dst = hdr->dst)
                     : sys_session_send_raw(to, data, len, sys_session_send_flags(hdr->frame));
  if (err == NULL) {
    return;
  }
  if (FRAME_KIND(hdr->frame) == SYS_SESSION_REQUEST && !(hdr->frame & ROUTER_FLAG_NOREPLY)) {
    const cmd_req_t cmd = request_cmd(hdr, data + HDR, len - HDR);
    answer_error(&cmd, err);
  } else {
    err_free(err);
  }
}

/* A broadcast request (dst 0xFF): run here and sent to every slave (a slave has none). It reaches a slave from its
 * master, and the master from a slave or a client. It is never answered. */
static void on_broadcast(const sys_session_hdr_t* hdr, const uint8_t* data, uint32_t len) {
  if (FRAME_KIND(hdr->frame) != SYS_SESSION_REQUEST) {
    return;
  }
  pass_to_router(hdr, data + HDR, len - HDR);
  err_free(send_down(data, len, hdr->frame));
}

// The provider's rx (ctx: the slot): a frame, or the provider up or down.
static void on_rx(void* ctx, sys_data_provider_id_t id, const uint8_t* data, uint32_t len) {
  (void)id;
  const int32_t at = (int32_t)(uintptr_t)ctx;
  sys_session_no_wait();
  if (data == NULL) {
    if (len > 0) {
      // Nobody may hear it yet (a BLE client not subscribed): the peer's hello asks again.
      err_free(sys_session_hello_send(at));
    } else {
      sys_session_end(at);
    }
    return;
  }
  if (len < HDR) {
    return;
  }
  sys_session_hdr_t hdr;
  memcpy(&hdr, data, HDR);
  const uint8_t kind = FRAME_KIND(hdr.frame);
  const uint8_t* body = data + HDR;
  const uint32_t body_len = len - HDR;
  if (kind == SYS_SESSION_CONTROL) {
    on_control(at, body, body_len);
    portENTER_CRITICAL(&s_lock);
    s_state.sessions[at].silent_ms = 0;
    portEXIT_CRITICAL(&s_lock);
    return;
  }

  // Anything else on an open session only, from the peer it was opened with.
  portENTER_CRITICAL(&s_lock);
  session_slot_t* s = &s_state.sessions[at];
  // A slave's or a client's frames carry its own id; the master's any (it relays the clients' to its slaves).
  const bool ok = session_is_open(s) && (s->peer == SYS_SESSION_MASTER || hdr.src == s->hello.id);
  if (ok) {
    s->silent_ms = 0;
  }
  portEXIT_CRITICAL(&s_lock);
  if (!ok) {
    sys_session_hello_start_over(at, hdr.src);
    return;
  }
  if (hdr.dst == SYS_SESSION_ID_BROADCAST) {
    on_broadcast(&hdr, data, len);
  } else if (hdr.dst != s_state.board_id) {
    forward(at, &hdr, data, len);
  } else if (kind == SYS_SESSION_REQUEST) {
    pass_to_router(&hdr, body, body_len);
  } else if (kind == SYS_SESSION_ANSWER) {
    sys_session_call_answer(&hdr, body, body_len);
  } else {
    tell_stream(&hdr, body, body_len);
  }
}

/* ----------------------------------------------------------------------------
 * The timer: session timeouts, pings, call timeouts
 * ------------------------------------------------------------------------- */

static void on_tick(void* arg) {
  (void)arg;
  sys_session_no_wait();
  const uint32_t now = (uint32_t)board_time_uptime_ms();
  for (int32_t at = 0; at < CONFIG_SYS_SESSION_MAX; at++) {
    portENTER_CRITICAL(&s_lock);
    session_slot_t* s = &s_state.sessions[at];
    const bool live = session_is_open(s);
    if (live) {
      s->silent_ms = (uint16_t)(s->silent_ms + TICK_MS);
      s->ping_ms = (uint16_t)(s->ping_ms + TICK_MS);
    }
    const bool silent = live && s->silent_ms > timeout_of(s->peer);
    const bool ping = live && s->ping_ms >= s->hello.timeout_ms / 3u;  // every third of the peer's timeout
    if (ping) {
      s->ping_ms = 0;
    }
    portEXIT_CRITICAL(&s_lock);
    if (silent) {
      sys_session_end(at);
    } else if (ping) {
      // Busy: the next ping is a third of the timeout later.
      err_free(sys_session_send_control(at, SYS_SESSION_PING, NULL, 0));
    }
  }
  sys_session_call_expire(now);
}

/* ----------------------------------------------------------------------------
 * Start and this board
 * ------------------------------------------------------------------------- */

// The board id kept in NVS (ID), the default when there is none (the default partition: sys_ble initialises it again).
static uint8_t load_board_id(void) {
  uint8_t id = CONFIG_SYS_SESSION_BOARD_ID_DEFAULT;
  nvs_handle_t nvs;
  if (nvs_flash_init() == ESP_OK && nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
    if (nvs_get_u8(nvs, NVS_KEY_ID, &id) != ESP_OK || id > SYS_SESSION_ID_LAST) {
      id = CONFIG_SYS_SESSION_BOARD_ID_DEFAULT;
    }
    nvs_close(nvs);
  }
  return id;
}

err_h sys_session_init(void) {
  s_state.board_id = load_board_id();
  ERR_TRY(sys_session_hello_init());
  if (s_state.timer == NULL) {
    const esp_timer_create_args_t args = {.callback = on_tick, .name = "session"};
    ERR_TRY_ESP(esp_timer_create(&args, &s_state.timer));
    ERR_TRY_ESP(esp_timer_start_periodic(s_state.timer, TICK_MS * 1000u));
  }
  return NULL;
}

uint8_t sys_session_get_board_id(void) {
  return s_state.board_id;
}

// No lock: one word, set by attach and detach.
bool sys_session_is_master(void) {
  return s_state.master < 0;
}

err_h sys_session_set_board_id(uint8_t board_id) {
  ERR_CHECK_IN_RANGE(board_id, 0, SYS_SESSION_ID_LAST, ERR_CMD_BAD_ARG);
  nvs_handle_t nvs;
  ERR_TRY_ESP(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs));
  esp_err_t rc = nvs_set_u8(nvs, NVS_KEY_ID, board_id);
  if (rc == ESP_OK) {
    rc = nvs_commit(nvs);
  }
  nvs_close(nvs);
  return ERR_ESP(rc);
}

err_h sys_session_set_timeout(uint32_t ms) {
  ERR_CHECK_IN_RANGE(ms, SYS_SESSION_TIMEOUT_MIN_MS, SYS_SESSION_TIMEOUT_MAX_MS, ERR_CMD_BAD_ARG);
  s_state.timeout_ms = ms;
  for (int32_t at = 0; at < CONFIG_SYS_SESSION_MAX; at++) {
    portENTER_CRITICAL(&s_lock);
    const session_slot_t* s = &s_state.sessions[at];
    const bool tell = session_is_open(s) && s->peer != SYS_SESSION_SLAVE;
    portEXIT_CRITICAL(&s_lock);
    if (tell) {
      err_report(sys_session_hello_send(at));
    }
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Attaching and detaching
 * ------------------------------------------------------------------------- */

// Session `at`'s slot is free again (its provider failed to open, or it was detached).
static void slot_free(int32_t at) {
  portENTER_CRITICAL(&s_lock);
  s_state.sessions[at].provider = (sys_data_provider_id_t){0};
  s_state.master = s_state.master == at ? -1 : s_state.master;
  portEXIT_CRITICAL(&s_lock);
}

err_h sys_session_attach(sys_data_provider_id_t provider, uint8_t peer) {
  ERR_CHECK(peer <= SYS_SESSION_CLIENT, ERR_CMD_BAD_ARG);
  bool attached = false;
  bool clash = false;  // a star: a board with a master has no other session
  int32_t at = -1;
  portENTER_CRITICAL(&s_lock);
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_slot_t* s = &s_state.sessions[i];
    if (!session_in_use(s)) {
      at = at < 0 ? i : at;
    } else {
      attached |= sys_data_provider_same_id(s->provider, provider);
      clash |= peer == SYS_SESSION_MASTER || s->peer == SYS_SESSION_MASTER;
    }
  }
  if (!attached && !clash && at >= 0) {
    s_state.sessions[at] = (session_slot_t){.provider = provider, .peer = peer, .hello = {.id = SYS_SESSION_ID_NONE}};
    s_state.master = peer == SYS_SESSION_MASTER ? at : s_state.master;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(!attached, ERR_CMD_BAD_ARG);
  ERR_CHECK(!clash, ERR_SESSION_ROLE);
  ERR_CHECK(at >= 0, ERR_SESSION_FULL);
  err_h err = sys_data_provider_open(provider, on_rx, (void*)(uintptr_t)at);
  if (err != NULL) {
    slot_free(at);
    return err;
  }
  // Up already (UART): the hello goes now; else when its rx says up.
  if (sys_data_provider_is_up(provider)) {
    err_free(sys_session_hello_send(at));
  }
  return NULL;
}

err_h sys_session_detach(sys_data_provider_id_t provider) {
  portENTER_CRITICAL(&s_lock);
  const int32_t at = session_find_by_provider(provider);
  portEXIT_CRITICAL(&s_lock);
  if (at < 0) {
    return NULL;
  }
  ERR_TRY(sys_data_provider_close(provider));
  sys_session_end(at);
  slot_free(at);
  return NULL;
}
