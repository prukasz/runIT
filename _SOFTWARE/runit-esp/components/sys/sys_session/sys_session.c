#include "sys_session.h"

#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "sys_session_frame.h"
#include "sys_session_internal.h"
#include "utils.h"

#define TICK_MS         10u   // the timer: pings and session timeouts
#define REJECT_BYTES    64u   // an error record answered on the RX task (its stack)
#define NVS_NAMESPACE   "session"
#define NVS_KEY_ID      "id"
#define HDR             ((uint32_t)sizeof(sys_session_hdr_t))
#define FRAME(kind)     ((uint8_t)((kind) << 6))

_Static_assert(CONFIG_SYS_SESSION_TIMEOUT_MS >= SYS_SESSION_TIMEOUT_MIN_MS, "CONFIG_SYS_SESSION_TIMEOUT_MS too short");
_Static_assert((ROUTER_FLAG_URGENT | ROUTER_FLAG_NOREPLY) == SYS_SESSION_FRAME_FLAGS, "the frame byte's flags");
_Static_assert(ROUTER_STATUS_ERROR == 1, "the frame byte's status bit");
_Static_assert(ROUTER_SRC_SELF == SYS_SESSION_ID_SELF, "the board's own commands");

/* A session: a provider and the peer it leads to. Its fields are under s_lock; nothing else runs under it (a send, a
 * listener and the router are called after it is released). */
typedef struct {
  sys_data_provider_id_t provider;
  bool used;
  bool up;                   // the provider carries frames (max_frame > 0)
  bool open;                 // both hellos passed
  uint8_t peer;              // sys_session_peer_e
  uint8_t access;            // sys_session_access_e
  uint8_t peer_id;           // the client id it was given; SYS_SESSION_ID_NONE until its hello
  uint16_t peer_timeout_ms;  // the peer's timeout of this session, from its hello: send every third of it
  uint32_t peer_boot_id;
  int64_t last_rx_us;
  int64_t last_tx_us;
} session_t;

static U_SPINLOCK_DEFINE(s_lock);

static struct {
  session_t sessions[CONFIG_SYS_SESSION_MAX];
  uint8_t board_id;
  uint32_t boot_id;
  uint8_t mac[6];
  uint32_t timeout_ms;  // this board's timeout of its clients' sessions (the dead-man's time)
  esp_timer_handle_t timer;
  sys_session_gone_f gone[CONFIG_SYS_SESSION_GONE_LISTENERS];
  sys_session_reached_f reached[CONFIG_SYS_SESSION_GONE_LISTENERS];
  sys_session_stream_f stream[CONFIG_SYS_SESSION_STREAM_LISTENERS];
} s_session = {.timeout_ms = CONFIG_SYS_SESSION_TIMEOUT_MS};

#define LOCK()   U_CRITICAL_ENTER(s_lock)
#define UNLOCK() U_CRITICAL_EXIT(s_lock)

static int64_t now_us(void) {
  return esp_timer_get_time();
}

/* ----------------------------------------------------------------------------
 * Sessions
 * ------------------------------------------------------------------------- */

static bool same_id(sys_data_provider_id_t a, sys_data_provider_id_t b) {
  return a.link == b.link && a.channel == b.channel;
}

/* The slot of `provider`, -1 when it is not attached. Under the lock. */
static int find_provider(sys_data_provider_id_t provider) {
  for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    if (s_session.sessions[i].used && same_id(s_session.sessions[i].provider, provider)) {
      return i;
    }
  }
  return -1;
}

/* The open session whose peer is `peer`, -1 when there is none. Under the lock. */
static int find_peer(uint8_t peer) {
  for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_t* s = &s_session.sessions[i];
    if (s->used && s->open && s->peer_id == peer) {
      return i;
    }
  }
  return -1;
}

// This board's timeout of a session of this kind.
static uint32_t timeout_of(uint8_t peer) {
  return peer == SYS_SESSION_CHILD ? CONFIG_SYS_SESSION_TIMEOUT_MS : s_session.timeout_ms;
}

/* One frame on a session's provider: the header, then `body`. `buf` has HDR bytes free in front of `body` when `body`
 * is buf + HDR; else the frame is built in `buf`. */
static err_h send_frame(int at, const sys_session_hdr_t* hdr, uint8_t* buf, const uint8_t* body, uint32_t len,
                        uint8_t flags) {
  LOCK();
  const sys_data_provider_id_t provider = s_session.sessions[at].provider;
  s_session.sessions[at].last_tx_us = now_us();
  UNLOCK();
  memcpy(buf, hdr, HDR);
  if (body != NULL && body != buf + HDR && len > 0) {
    memmove(buf + HDR, body, len);
  }
  return sys_data_provider_send(provider, buf, HDR + len, flags);
}

static err_h send_control(int at, uint8_t op, const uint8_t* data, uint32_t len) {
  uint8_t buf[HDR + 1u + sizeof(sys_session_hello_t)];
  LOCK();
  const uint8_t dst = s_session.sessions[at].peer_id;
  UNLOCK();
  const sys_session_hdr_t hdr = {.frame = FRAME(SYS_SESSION_CONTROL), .dst = dst, .src = s_session.board_id};
  buf[HDR] = op;
  if (len > 0) {
    memcpy(buf + HDR + 1, data, len);
  }
  return send_frame(at, &hdr, buf, buf + HDR, 1u + len, SYS_DATA_PROVIDER_SEND_NOWAIT);
}

static err_h send_hello(int at) {
  LOCK();
  const session_t* s = &s_session.sessions[at];
  const sys_session_hello_t hello = {
      .version = SYS_SESSION_VERSION,
      .board_id = s_session.board_id,
      .your_id = s->peer == SYS_SESSION_CLIENT ? s->peer_id : SYS_SESSION_ID_NONE,
      .boot_id = s_session.boot_id,
      .timeout_ms = (uint16_t)timeout_of(s->peer),
  };
  UNLOCK();
  sys_session_hello_t out = hello;
  memcpy(out.mac, s_session.mac, sizeof(out.mac));
  return send_control(at, SYS_SESSION_HELLO, (const uint8_t*)&out, sizeof(out));
}

// A frame before the session opened: the peer starts over (NO_SESSION, then this board's hello).
static void no_session(int at, uint8_t src) {
  err_report(ERR_NEW(ERR_SESSION_NONE, .src = src));
  err_free(send_control(at, SYS_SESSION_NO_SESSION, NULL, 0));
  err_free(send_hello(at));
}

static void tell_gone(uint8_t peer, uint8_t via) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_session.gone) && s_session.gone[i] != NULL; i++) {
    s_session.gone[i](peer, via);
  }
}

static void tell_reached(uint8_t peer) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_session.reached) && s_session.reached[i] != NULL; i++) {
    s_session.reached[i](peer);
  }
}

// Ends a session: its peer is gone. The client's id is free again.
static void end_session(int at) {
  LOCK();
  session_t* s = &s_session.sessions[at];
  const bool was_open = s->open;
  const uint8_t peer = s->peer_id;
  const uint8_t via = s->peer;
  s->open = false;
  if (s->peer == SYS_SESSION_CLIENT) {
    s->peer_id = SYS_SESSION_ID_NONE;
  }
  UNLOCK();
  if (was_open) {
    tell_gone(peer, via);
  }
}

/* ----------------------------------------------------------------------------
 * Answers
 * ------------------------------------------------------------------------- */

/* The router's done for a peer's command: the answer frame in the room in front of the data, to the session of the
 * command's src. It waits for room in the provider's queue at most the driver's bound. */
static err_h session_answer(void* ctx, const err_cmd_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  (void)ctx;
  LOCK();
  const int at = find_peer(cmd->src);
  UNLOCK();
  ERR_CHECK(at >= 0, ERR_SESSION_NO_ROUTE, .dst = cmd->src);
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_ANSWER) | (status == ROUTER_STATUS_ERROR ? SYS_SESSION_FRAME_STATUS : 0)),
      .dst = cmd->src,
      .src = s_session.board_id,
      .seq = cmd->seq,
  };
  const uint8_t flags = (cmd->flags & ROUTER_FLAG_URGENT) ? SYS_DATA_PROVIDER_SEND_URGENT : 0;
  return send_frame(at, &hdr, data - HDR, data, len, flags);
}

// A request this board does not run (refused, no route): its error record to its source, at once.
static void answer_error(int at, const err_cmd_t* cmd, err_h err) {
  uint8_t buf[HDR + REJECT_BYTES];
  const uint32_t n = err_record(cmd, err, buf + HDR, REJECT_BYTES);
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_ANSWER) | SYS_SESSION_FRAME_STATUS),
      .dst = cmd->src,
      .src = s_session.board_id,
      .seq = cmd->seq,
  };
  err_report(send_frame(at, &hdr, buf, buf + HDR, n, SYS_DATA_PROVIDER_SEND_NOWAIT));
}

/* ----------------------------------------------------------------------------
 * What comes in (the driver's RX task)
 * ------------------------------------------------------------------------- */

static void on_hello(int at, const uint8_t* body, uint32_t len) {
  sys_session_hello_t hello;
  if (len < sizeof(hello)) {
    return;
  }
  memcpy(&hello, body, sizeof(hello));
  if (hello.version != SYS_SESSION_VERSION) {
    err_report(ERR_NEW(ERR_SESSION_VERSION, .got = hello.version, .mine = SYS_SESSION_VERSION));
    return;
  }
  LOCK();
  const bool restarted = s_session.sessions[at].open && s_session.sessions[at].peer_boot_id != hello.boot_id;
  UNLOCK();
  if (restarted) {
    end_session(at);  // the peer restarted: what it had here is gone
  }

  LOCK();
  session_t* s = &s_session.sessions[at];
  const bool was_open = s->open;
  bool full = false;
  if (s->peer_id == SYS_SESSION_ID_NONE) {
    // A client's id: the first one no other session has.
    full = true;
    for (uint8_t id = SYS_SESSION_ID_CLIENT_FIRST; id <= SYS_SESSION_ID_CLIENT_LAST && full; id++) {
      bool taken = false;
      for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
        taken |= s_session.sessions[i].used && s_session.sessions[i].peer_id == id;
      }
      if (!taken) {
        s->peer_id = id;
        full = false;
      }
    }
  }
  if (!full) {
    s->open = true;
    s->peer_boot_id = hello.boot_id;
    s->peer_timeout_ms = hello.timeout_ms;
    s->last_rx_us = now_us();
  }
  const uint8_t peer = s->peer_id;
  UNLOCK();
  if (full) {
    err_report(ERR_NEW(ERR_SESSION_FULL));
    return;
  }
  // A client is answered every hello (it learns its id there); the session opened: the peer is reached.
  err_report(send_hello(at));
  if (!was_open) {
    tell_reached(peer);
  }
}

static void on_control(int at, const uint8_t* body, uint32_t len) {
  if (len == 0) {
    return;
  }
  switch (body[0]) {
    case SYS_SESSION_HELLO:
      on_hello(at, body + 1, len - 1);
      break;
    case SYS_SESSION_NO_SESSION:
      end_session(at);
      err_report(send_hello(at));
      break;
    default:  // PING: its arrival is all it says
      break;
  }
}

static void on_request(int at, const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  err_cmd_t cmd = {.req = body, .req_len = len, .seq = hdr->seq, .src = hdr->src,
                   .flags = (uint8_t)(hdr->frame & SYS_SESSION_FRAME_FLAGS)};
  if (hdr->dst == SYS_SESSION_ID_BROADCAST) {
    cmd.flags |= ROUTER_FLAG_NOREPLY;  // a broadcast is never answered
  }
  LOCK();
  const uint8_t access = s_session.sessions[at].access;
  UNLOCK();
  err_h err = NULL;
  if (hdr->dst != s_session.board_id && hdr->dst != SYS_SESSION_ID_BROADCAST) {
    err = ERR_NEW(ERR_SESSION_NO_ROUTE, .dst = hdr->dst);
  } else if (access == SYS_SESSION_READ && len > 1 && body[1] == CMD_DIR_WRITE) {
    err = ERR_NEW(ERR_SESSION_DENIED, .src = hdr->src);
  } else {
    err = router_submit(&cmd, session_answer, NULL);  // copied; never blocks
  }
  if (err == NULL) {
    return;
  }
  if (!(cmd.flags & ROUTER_FLAG_NOREPLY)) {
    answer_error(at, &cmd, err);
  }
  router_report_cmd(&cmd, err);
}

static void on_stream(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_session.stream) && s_session.stream[i] != NULL; i++) {
    s_session.stream[i](hdr->src, hdr->seq, body, len);
  }
}

// The provider's rx (ctx: the slot): a frame, or the provider up or down.
static void on_rx(void* ctx, sys_data_provider_id_t id, const uint8_t* data, uint32_t len) {
  (void)id;
  const int at = (int)(uintptr_t)ctx;
  if (data == NULL) {
    LOCK();
    s_session.sessions[at].up = len > 0;
    UNLOCK();
    if (len > 0) {
      err_free(send_hello(at));  // nobody may hear it yet (a BLE client not subscribed): the peer's hello asks again
    } else {
      end_session(at);
    }
    return;
  }
  sys_session_hdr_t hdr;
  if (len < HDR) {
    return;
  }
  memcpy(&hdr, data, HDR);
  const uint8_t kind = (uint8_t)((hdr.frame & SYS_SESSION_FRAME_KIND) >> 6);
  const uint8_t* body = data + HDR;
  const uint32_t body_len = len - HDR;
  if (kind == SYS_SESSION_CONTROL) {
    on_control(at, body, body_len);
    LOCK();
    s_session.sessions[at].last_rx_us = now_us();
    UNLOCK();
    return;
  }

  // Anything else on an open session only, from the peer it was opened with.
  LOCK();
  session_t* s = &s_session.sessions[at];
  const bool ok = s->open && hdr.src == s->peer_id;
  if (ok) {
    s->last_rx_us = now_us();
  }
  UNLOCK();
  if (!ok) {
    no_session(at, hdr.src);
    return;
  }
  if (kind == SYS_SESSION_REQUEST) {
    on_request(at, &hdr, body, body_len);
  } else if (kind == SYS_SESSION_STREAM && hdr.dst == s_session.board_id) {
    on_stream(&hdr, body, body_len);
  }
  // An answer: this board makes no call to another board, so none is waited for.
}

/* ----------------------------------------------------------------------------
 * The timer: session timeouts and pings
 * ------------------------------------------------------------------------- */

static void on_tick(void* arg) {
  (void)arg;
  const int64_t now = now_us();
  for (int at = 0; at < CONFIG_SYS_SESSION_MAX; at++) {
    LOCK();
    const session_t* s = &s_session.sessions[at];
    const bool live = s->used && s->open;
    const bool silent = live && now - s->last_rx_us > (int64_t)timeout_of(s->peer) * 1000;
    const bool ping = live && s->up && s->peer_timeout_ms > 0 &&
                      now - s->last_tx_us >= (int64_t)s->peer_timeout_ms * 1000 / 3;
    UNLOCK();
    if (silent) {
      end_session(at);
    } else if (ping) {
      err_free(send_control(at, SYS_SESSION_PING, NULL, 0));  // busy: the next tick tries again
    }
  }
}

/* ----------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

err_h sys_session_init(void) {
  // The board id: NVS, the default when there is none (the default partition: sys_ble initialises it again).
  uint8_t id = SYS_SESSION_ID_DEFAULT;
  esp_err_t rc = nvs_flash_init();
  if (rc == ESP_OK) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
      if (nvs_get_u8(nvs, NVS_KEY_ID, &id) != ESP_OK || id > SYS_SESSION_ID_LAST) {
        id = SYS_SESSION_ID_DEFAULT;
      }
      nvs_close(nvs);
    }
  }
  s_session.board_id = id;
  s_session.boot_id = esp_random();
  ERR_TRY_ESP(esp_efuse_mac_get_default(s_session.mac));
  if (s_session.timer == NULL) {
    const esp_timer_create_args_t args = {.callback = on_tick, .name = "session"};
    ERR_TRY_ESP(esp_timer_create(&args, &s_session.timer));
    ERR_TRY_ESP(esp_timer_start_periodic(s_session.timer, TICK_MS * 1000u));
  }
  return NULL;
}

uint8_t sys_session_board_id(void) {
  return s_session.board_id;
}

err_h sys_session_set_board_id(uint8_t board_id) {
  ERR_CHECK(board_id <= SYS_SESSION_ID_LAST, ERR_CMD_BAD_ARG);
  nvs_handle_t nvs;
  ERR_TRY_ESP(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs));
  esp_err_t rc = nvs_set_u8(nvs, NVS_KEY_ID, board_id);
  if (rc == ESP_OK) {
    rc = nvs_commit(nvs);
  }
  nvs_close(nvs);
  return ERR_ESP(rc);
}

err_h sys_session_attach(sys_data_provider_id_t provider, uint8_t peer, uint8_t access) {
  ERR_CHECK(peer == SYS_SESSION_CLIENT, ERR_SESSION_CLIENT);
  ERR_CHECK(access <= SYS_SESSION_FULL, ERR_CMD_BAD_ARG);
  ERR_CHECK(sys_data_provider_state(provider) & SYS_DATA_PROVIDER_STATE_FRAMED, ERR_SESSION_NOT_FRAMED,
            .link = provider.link, .channel = provider.channel);
  LOCK();
  const bool attached = find_provider(provider) >= 0;
  int at = -1;
  for (int i = 0; !attached && i < CONFIG_SYS_SESSION_MAX && at < 0; i++) {
    if (!s_session.sessions[i].used) {
      at = i;
      s_session.sessions[i] = (session_t){
          .provider = provider,
          .used = true,
          .peer = peer,
          .access = access,
          .peer_id = SYS_SESSION_ID_NONE,
      };
    }
  }
  UNLOCK();
  ERR_CHECK(!attached, ERR_CMD_BAD_ARG);
  ERR_CHECK(at >= 0, ERR_SESSION_FULL);
  err_h err = sys_data_provider_open(provider, on_rx, (void*)(uintptr_t)at);
  if (err != NULL) {
    LOCK();
    s_session.sessions[at].used = false;
    UNLOCK();
    return err;
  }
  // Up already (UART): the hello goes now; else when its rx says up.
  if (sys_data_provider_max_frame(provider) > 0) {
    on_rx((void*)(uintptr_t)at, provider, NULL, sys_data_provider_max_frame(provider));
  }
  return NULL;
}

err_h sys_session_detach(sys_data_provider_id_t provider) {
  LOCK();
  const int at = find_provider(provider);
  UNLOCK();
  if (at < 0) {
    return NULL;
  }
  ERR_TRY(sys_data_provider_close(provider));
  end_session(at);
  LOCK();
  s_session.sessions[at].used = false;
  UNLOCK();
  return NULL;
}

err_h sys_session_set_timeout(uint32_t ms) {
  ERR_CHECK(ms >= SYS_SESSION_TIMEOUT_MIN_MS && ms <= SYS_SESSION_TIMEOUT_MAX_MS, ERR_CMD_BAD_ARG);
  s_session.timeout_ms = ms;
  for (int at = 0; at < CONFIG_SYS_SESSION_MAX; at++) {
    LOCK();
    const session_t* s = &s_session.sessions[at];
    const bool tell = s->used && s->open && s->peer != SYS_SESSION_CHILD;
    UNLOCK();
    if (tell) {
      err_report(send_hello(at));
    }
  }
  return NULL;
}

err_h sys_session_call(uint8_t dst, cmd_in_t req, uint8_t flags, router_done_f cb, void* ctx) {
  ERR_CHECK(dst == s_session.board_id || dst == SYS_SESSION_ID_SELF, ERR_SESSION_NO_ROUTE, .dst = dst);
  return router_inject(req, flags, cb, ctx);
}

bool sys_session_wired(uint8_t peer) {
  LOCK();
  const int at = find_peer(peer);
  const sys_data_provider_id_t provider = at >= 0 ? s_session.sessions[at].provider : (sys_data_provider_id_t){0};
  UNLOCK();
  return at >= 0 && (sys_data_provider_state(provider) & SYS_DATA_PROVIDER_STATE_WIRED);
}

err_h sys_session_stream(uint8_t peer, uint16_t seq, const uint8_t* data, uint32_t len, uint8_t flags) {
  ERR_CHECK(data != NULL && len > 0 && len <= CMD_DATA_MAX, ERR_CMD_BAD_ARG);
  LOCK();
  const int at = find_peer(peer);
  UNLOCK();
  ERR_CHECK(at >= 0, ERR_SESSION_NO_ROUTE, .dst = peer);
  uint8_t buf[CMD_FRAME_MAX];
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_STREAM) | (flags & SYS_SESSION_FRAME_FLAGS)),
      .dst = peer,
      .src = s_session.board_id,
      .seq = seq,
  };
  const uint8_t urgent = (flags & ROUTER_FLAG_URGENT) ? SYS_DATA_PROVIDER_SEND_URGENT : 0;
  return send_frame(at, &hdr, buf, data, len, SYS_DATA_PROVIDER_SEND_NOWAIT | urgent);
}

bool sys_session_below(uint8_t peer) {
  LOCK();
  const int at = find_peer(peer);
  const bool below = at >= 0 && s_session.sessions[at].peer != SYS_SESSION_PARENT;
  UNLOCK();
  return below;
}

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
  ADD_LISTENER(s_session.gone, fn);
}

err_h sys_session_on_reached(sys_session_reached_f fn) {
  ADD_LISTENER(s_session.reached, fn);
}

err_h sys_session_on_stream(sys_session_stream_f fn) {
  ADD_LISTENER(s_session.stream, fn);
}

/* ----------------------------------------------------------------------------
 * The router's
 * ------------------------------------------------------------------------- */

uint8_t sys_session_list(sys_session_info_t out[CONFIG_SYS_SESSION_MAX]) {
  uint8_t count = 0;
  LOCK();
  for (int i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_t* s = &s_session.sessions[i];
    if (s->used) {
      out[count++] = (sys_session_info_t){
          .provider = s->provider,
          .peer = s->peer,
          .access = s->access,
          .open = s->open,
          .peer_id = s->peer_id,
          .timeout_ms = (uint16_t)timeout_of(s->peer),
          .subtree = 0,
      };
    }
  }
  UNLOCK();
  return count;
}
