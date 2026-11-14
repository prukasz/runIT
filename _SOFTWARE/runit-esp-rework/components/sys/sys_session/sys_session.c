#include "sys_session.h"

#include <string.h>

#include "board_time.h"
#include "board_timer.h"
#include "sdkconfig.h"
#include "sys_session_internal.h"
#include "sys_session_router.h"
#include "utils.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_SESSION, "sys_session", ERR_SYS_SESSION_MAP)
MODULE_ID_DEFINE(SESSION, 0x0E)

#define TICK_MS         10u   // the timer: pings, session timeouts, call timeouts
#define HELLO_RETRY_MS  1000u // a DOWN way not open says hello again this often
#define DATA_HEAD       3u    // [SYS_SESSION_CHANNEL_DATA][to id][from id] in front of a data frame's data
#define REJECT_BYTES    64u   // an error record answered on the RX task (its stack)
#define CONTROL_MAX     ((uint32_t)sizeof(sys_session_hello_t))  // the largest control frame's data: the hello

_Static_assert(CONFIG_SYS_SESSION_TIMEOUT_MS >= SYS_SESSION_TIMEOUT_MIN_MS, "CONFIG_SYS_SESSION_TIMEOUT_MS too short");
_Static_assert((ROUTER_FLAG_URGENT | ROUTER_FLAG_NOREPLY) == SYS_SESSION_FRAME_FLAGS, "the frame byte's flags");
_Static_assert(ROUTER_STATUS_ERROR == 1, "the frame byte's status bit");
_Static_assert(ROUTER_SRC_SELF == SYS_SESSION_ID_SELF, "the board's own commands");
_Static_assert(ROUTER_SRC_NONE == SYS_SESSION_ID_NONE, "a command with no peer");
_Static_assert(SYS_SESSION_TIMEOUT_MAX_MS + TICK_MS <= UINT16_MAX, "a session's silence and ping in 16 bits");

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

#define PUSH_CLASS(frame) (((frame) & ROUTER_FLAG_URGENT) ? 1u : 0u)  // a PUSH's class: 0 normal, 1 urgent

/* The PUSH counters of one peer, by the other end's id (not by session: a slave sends to the root and to each of its
 * clients through one session). Both directions: this board's seq to it, and its last seq to this board. */
typedef struct {
  uint8_t peer;     // SYS_SESSION_ID_NONE: free
  uint8_t have;     // bit per class: rx holds a seq
  uint16_t tx[2];   // the next seq to it, per class (counted also when the provider refused the frame)
  uint16_t rx[2];   // its last seq to this board, per class
} session_push_t;

static struct {
  session_slot_t sessions[CONFIG_SYS_SESSION_MAX];
  session_push_t counts[CONFIG_SYS_SESSION_PUSH_PEERS];
  uint8_t board_id;
  uint32_t timeout_ms;  // this board's timeout of its master's and its clients' sessions (the dead-man's time)
  board_timer_t timer;
  sys_session_gone_f gone[CONFIG_SYS_SESSION_PEER_LISTENERS];
  sys_session_reached_f reached[CONFIG_SYS_SESSION_PEER_LISTENERS];
  sys_session_push_f push[CONFIG_SYS_SESSION_PUSH_LISTENERS];
} s_state = {.timeout_ms = CONFIG_SYS_SESSION_TIMEOUT_MS};

/* ----------------------------------------------------------------------------
 * The sessions: lookups, the way to a peer, copies (under the lock)
 * ------------------------------------------------------------------------- */

static int32_t find_by_provider(uint8_t provider) {
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    if (s_state.sessions[i].used && s_state.sessions[i].provider == provider) {
      return i;
    }
  }
  return -1;
}

// The open session whose peer has this board or client id, -1 when there is none.
static int32_t find_by_peer_id(uint8_t peer_id) {
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    if (session_is_open(&s_state.sessions[i]) && s_state.sessions[i].hello.id == peer_id) {
      return i;
    }
  }
  return -1;
}

// The master's session: open, on an UP way, with a board's id. -1: none, this board is the root.
static int32_t master_at(void) {
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_slot_t* s = &s_state.sessions[i];
    if (session_is_open(s) && sys_session_role(s) == SYS_SESSION_MASTER) {
      return i;
    }
  }
  return -1;
}

// A slave reaches everything through its master; the root through the open session of that slave or client.
static int32_t route_locked(uint8_t dst) {
  const int32_t master = master_at();
  return master >= 0 ? master : find_by_peer_id(dst);
}

static uint32_t timeout_of(const session_slot_t* s) {
  return sys_session_role(s) == SYS_SESSION_SLAVE ? CONFIG_SYS_SESSION_TIMEOUT_MS : s_state.timeout_ms;
}

int32_t sys_session_route(uint8_t dst) {
  portENTER_CRITICAL(&s_lock);
  const int32_t at = route_locked(dst);
  portEXIT_CRITICAL(&s_lock);
  return at;
}

bool sys_session_wired(uint8_t peer) {
  portENTER_CRITICAL(&s_lock);
  const int32_t at = find_by_peer_id(peer);
  const uint8_t provider = at >= 0 ? s_state.sessions[at].provider : 0;
  portEXIT_CRITICAL(&s_lock);
  return at >= 0 && sys_data_provider_is_wired(provider);
}

uint32_t sys_session_timeout(int32_t at) {
  portENTER_CRITICAL(&s_lock);
  const uint32_t ms = timeout_of(&s_state.sessions[at]);
  portEXIT_CRITICAL(&s_lock);
  return ms;
}

session_slot_t sys_session_slot(int32_t at) {
  portENTER_CRITICAL(&s_lock);
  const session_slot_t slot = s_state.sessions[at];
  portEXIT_CRITICAL(&s_lock);
  return slot;
}

bool sys_session_admits(int32_t at) {
  portENTER_CRITICAL(&s_lock);
  const int32_t master = master_at();
  portEXIT_CRITICAL(&s_lock);
  return master < 0 || master == at;
}

bool sys_session_is_root(void) {
  portENTER_CRITICAL(&s_lock);
  const int32_t master = master_at();
  portEXIT_CRITICAL(&s_lock);
  return master < 0;
}

uint8_t sys_session_list(sys_session_info_t out[CONFIG_SYS_SESSION_MAX]) {
  uint8_t count = 0;
  portENTER_CRITICAL(&s_lock);
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    const session_slot_t* s = &s_state.sessions[i];
    if (s->used) {
      out[count++] = (sys_session_info_t){
          .provider = s->provider,
          .way = s->way,
          .role = sys_session_role(s),
          .open = session_is_open(s),
          .peer_id = s->hello.id,
          .build_id = s->hello.build_id,
          .timeout_ms = (uint16_t)timeout_of(s),
      };
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return count;
}

/* ----------------------------------------------------------------------------
 * Frames out: every frame is two parts, the header and the body, joined only in the provider's queue
 * ------------------------------------------------------------------------- */

/* Every frame to a peer leaves here, so a frame sent is the peer's sign of life: the next ping waits a third of its
 * timeout from it (a ping only on a session with nothing else to send). */
err_h sys_session_send_parts(int32_t at, const void* head, uint32_t head_len, const void* body, uint32_t body_len,
                             uint8_t flags) {
  portENTER_CRITICAL(&s_lock);
  const bool used = at >= 0 && at < CONFIG_SYS_SESSION_MAX && s_state.sessions[at].used;
  const uint8_t provider = used ? s_state.sessions[at].provider : 0;
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(used, ERR_SESSION_PEER_DOWN);
  err_h err = sys_data_provider_send(provider, head, head_len, body, body_len, flags);
  if (err == NULL) {
    portENTER_CRITICAL(&s_lock);
    session_slot_t* s = &s_state.sessions[at];
    if (s->used && s->provider == provider) {  // still the session it was sent on
      s->ping_ms = 0;
    }
    portEXIT_CRITICAL(&s_lock);
  }
  return err;
}

err_h sys_session_send_control(int32_t at, uint8_t op, const void* data, uint32_t len) {
  ERR_CHECK(len <= CONTROL_MAX, ERR_CMD_BAD_LEN, .got = (uint16_t)len, .need = CONTROL_MAX);
  portENTER_CRITICAL(&s_lock);
  const uint8_t dst = s_state.sessions[at].hello.id;
  portEXIT_CRITICAL(&s_lock);
  struct __attribute__((packed)) {
    sys_session_hdr_t hdr;
    uint8_t op;
  } head = {.hdr = {.frame = FRAME(SYS_SESSION_CONTROL), .dst = dst, .src = s_state.board_id}, .op = op};
  return sys_session_send_parts(at, &head, sizeof(head), data, len, SYS_DATA_PROVIDER_SEND_NOWAIT);
}

/* ----------------------------------------------------------------------------
 * PUSH counters (under the lock): seq of a PUSH is its sender's counter of (dst, class), for every stream and channel
 * ------------------------------------------------------------------------- */

// The counters of `peer`, a free entry taken for it when it has none; NULL: the table is full.
static session_push_t* counter_locked(uint8_t peer) {
  session_push_t* spare = NULL;
  for (uint32_t i = 0; i < ARRAY_LEN(s_state.counts); i++) {
    session_push_t* e = &s_state.counts[i];
    if (e->peer == peer) {
      return e;
    }
    if (spare == NULL && e->peer == SYS_SESSION_ID_NONE) {
      spare = e;
    }
  }
  if (spare != NULL) {
    *spare = (session_push_t){.peer = peer};
  }
  return spare;
}

// `peer` reached or gone: its counters start again. SYS_SESSION_ID_BROADCAST: every peer's (the master's session).
static void counter_forget_locked(uint8_t peer) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_state.counts); i++) {
    if (peer == SYS_SESSION_ID_BROADCAST || s_state.counts[i].peer == peer) {
      s_state.counts[i].peer = SYS_SESSION_ID_NONE;
    }
  }
}

/* The header of a PUSH to `peer`: the next seq of its class, or (the table full) seq 0 and UNCOUNTED: the frame still
 * goes, so a tunnel never depends on this board's table. */
static sys_session_hdr_t push_hdr(uint8_t peer, uint8_t flags) {
  sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_PUSH) | (flags & SYS_SESSION_FRAME_FLAGS)),
      .dst = peer,
      .src = s_state.board_id,
  };
  portENTER_CRITICAL(&s_lock);
  session_push_t* e = counter_locked(peer);
  if (e != NULL) {
    hdr.seq = e->tx[PUSH_CLASS(hdr.frame)]++;
  }
  portEXIT_CRITICAL(&s_lock);
  if (e == NULL) {
    hdr.frame |= SYS_SESSION_FRAME_UNCOUNTED;
  }
  return hdr;
}

/* The PUSH frames of this one's class from its src lost before it: the gap in src's counter. 0 for the first one, an
 * uncounted one, and when this board's table is full (not known). */
static uint16_t push_lost(const sys_session_hdr_t* hdr) {
  if (hdr->frame & SYS_SESSION_FRAME_UNCOUNTED) {
    return 0;
  }
  const uint32_t c = PUSH_CLASS(hdr->frame);
  uint16_t lost = 0;
  portENTER_CRITICAL(&s_lock);
  session_push_t* e = counter_locked(hdr->src);
  if (e != NULL) {
    if (e->have & (1u << c)) {
      lost = (uint16_t)(hdr->seq - e->rx[c] - 1u);
    }
    e->rx[c] = hdr->seq;
    e->have |= (uint8_t)(1u << c);
  }
  portEXIT_CRITICAL(&s_lock);
  return lost;
}

err_h sys_session_push(uint8_t peer, const uint8_t* data, uint32_t len, uint8_t flags) {
  ERR_CHECK(peer <= SYS_SESSION_ID_CLIENT_LAST && data != NULL && len > 0 && len <= CMD_DATA_MAX, ERR_CMD_BAD_ARG);
  const int32_t at = sys_session_route(peer);
  ERR_CHECK(at >= 0, ERR_SESSION_NO_ROUTE, .dst = peer);
  const sys_session_hdr_t hdr = push_hdr(peer, flags);
  return sys_session_send_parts(at, &hdr, HDR, data, len, sys_session_send_flags(hdr.frame));
}

err_h sys_session_data(uint8_t peer, uint8_t to, uint8_t from, const uint8_t* data, uint32_t len, uint8_t flags) {
  ERR_CHECK(peer <= SYS_SESSION_ID_CLIENT_LAST && data != NULL && len > 0, ERR_CMD_BAD_ARG);
  ERR_CHECK(len <= CMD_DATA_MAX - DATA_HEAD, ERR_CMD_BAD_LEN, .got = (uint16_t)len, .need = CMD_DATA_MAX - DATA_HEAD);
  const int32_t at = sys_session_route(peer);
  ERR_CHECK(at >= 0, ERR_SESSION_NO_ROUTE, .dst = peer);
  struct __attribute__((packed)) {
    sys_session_hdr_t hdr;
    uint8_t channel;
    uint8_t to;
    uint8_t from;
  } head = {
      .hdr = push_hdr(peer, flags),
      .channel = SYS_SESSION_CHANNEL_DATA,
      .to = to,
      .from = from,
  };
  return sys_session_send_parts(at, &head, sizeof(head), data, len, sys_session_send_flags(head.hdr.frame));
}

// A frame to every open slave except `skip` (-1: all); the first error comes back, the rest are still sent.
static err_h send_down(int32_t skip, const void* head, uint32_t head_len, const void* body, uint32_t body_len,
                       uint8_t frame) {
  err_h first = NULL;
  for (int32_t at = 0; at < CONFIG_SYS_SESSION_MAX; at++) {
    if (at == skip) {
      continue;
    }
    portENTER_CRITICAL(&s_lock);
    const session_slot_t* s = &s_state.sessions[at];
    const bool slave = session_is_open(s) && sys_session_role(s) == SYS_SESSION_SLAVE;
    portEXIT_CRITICAL(&s_lock);
    if (slave) {
      ERR_KEEP_FIRST(first, sys_session_send_parts(at, head, head_len, body, body_len, sys_session_send_flags(frame)));
    }
  }
  return first;
}

err_h sys_session_send_broadcast(const sys_session_hdr_t* hdr, cmd_in_t req, uint8_t flags) {
  (void)flags;
  portENTER_CRITICAL(&s_lock);
  const int32_t master = master_at();
  portEXIT_CRITICAL(&s_lock);
  if (master < 0) {
    return send_down(-1, hdr, HDR, req.data, req.len, hdr->frame);
  }
  return sys_session_send_parts(master, hdr, HDR, req.data, req.len, sys_session_send_flags(hdr->frame));
}

/* PEER to every open session of the root but `skip`: its session with `id` opened or ended. A slave hears of a client
 * that went (what the client had on it ends), the app of a slave that came or went (it adds its watches again). */
static void announce(int32_t skip, uint8_t id, bool up) {
  if (!sys_session_is_root()) {
    return;
  }
  const sys_session_peer_t peer = {.id = id, .up = up ? 1u : 0u};
  for (int32_t at = 0; at < CONFIG_SYS_SESSION_MAX; at++) {
    portENTER_CRITICAL(&s_lock);
    const bool tell = at != skip && session_is_open(&s_state.sessions[at]);
    portEXIT_CRITICAL(&s_lock);
    if (tell) {
      err_free(sys_session_send_control(at, SYS_SESSION_PEER, &peer, sizeof(peer)));  // a full queue: the peer's loss
    }
  }
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

err_h sys_session_on_push(sys_session_push_f fn) {
  ADD_LISTENER(s_state.push, fn);
}

static void tell_gone(uint8_t peer, uint8_t role) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_state.gone) && s_state.gone[i] != NULL; i++) {
    s_state.gone[i](peer, role);
  }
}

static void tell_reached(uint8_t peer) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_state.reached) && s_state.reached[i] != NULL; i++) {
    s_state.reached[i](peer);
  }
}

/* Data for a provider of this board, [SYS_SESSION_CHANNEL_DATA][to id][from id][data]: it leaves with no header. On the
 * driver's RX task, so NOWAIT; what can't go is dropped, as a PUSH frame that can't go on is (a gap in seq). */
static void tell_provider(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  if (len > DATA_HEAD) {
    err_free(sys_data_provider_send(body[1], NULL, 0, body + DATA_HEAD, len - DATA_HEAD,
                                    sys_session_send_flags(hdr->frame)));
  }
}

static void tell_push(const sys_session_hdr_t* hdr, uint16_t lost, const uint8_t* body, uint32_t len) {
  for (uint32_t i = 0; i < ARRAY_LEN(s_state.push) && s_state.push[i] != NULL; i++) {
    s_state.push[i](hdr->src, lost, body, len);
  }
}

/* ----------------------------------------------------------------------------
 * Opening and ending a session
 * ------------------------------------------------------------------------- */

bool sys_session_open(int32_t at, const sys_session_hello_state_t* peer, bool* was_open) {
  bool taken = false;
  portENTER_CRITICAL(&s_lock);
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    taken |= i != at && session_is_open(&s_state.sessions[i]) && s_state.sessions[i].hello.id == peer->id;
  }
  session_slot_t* s = &s_state.sessions[at];
  *was_open = session_is_open(s);
  if (!taken) {
    s->hello = *peer;
    s->silent_ms = 0;
    s->none_told = false;
    if (!*was_open) {
      counter_forget_locked(peer->id);  // a peer reached counts from the start
    }
  }
  portEXIT_CRITICAL(&s_lock);
  if (!taken && !*was_open) {
    tell_reached(peer->id);
    announce(at, peer->id, true);
  }
  return !taken;
}

void sys_session_end(int32_t at) {
  portENTER_CRITICAL(&s_lock);
  session_slot_t* s = &s_state.sessions[at];
  const bool was_open = session_is_open(s);
  const uint8_t peer = s->hello.id;
  const uint8_t role = sys_session_role(s);
  s->hello.id = SYS_SESSION_ID_NONE;
  s->none_told = false;  // the next stray frame is reported, once
  if (was_open) {
    counter_forget_locked(role == SYS_SESSION_MASTER ? SYS_SESSION_ID_BROADCAST : peer);
  }
  portEXIT_CRITICAL(&s_lock);
  if (!was_open) {
    return;
  }
  sys_session_call_fail_via(at);
  if (role == SYS_SESSION_MASTER) {
    tell_gone(SYS_SESSION_ID_BROADCAST, role);  // every peer: a slave reaches all of them through its master
    return;
  }
  tell_gone(peer, role);
  announce(at, peer, false);
}

void sys_session_end_others(int32_t at) {
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    portENTER_CRITICAL(&s_lock);
    const bool open = i != at && session_is_open(&s_state.sessions[i]);
    portEXIT_CRITICAL(&s_lock);
    if (open) {
      sys_session_end(i);
    }
  }
}

void sys_session_start_over(int32_t at, uint8_t src) {
  portENTER_CRITICAL(&s_lock);
  const bool tell = !s_state.sessions[at].none_told;
  s_state.sessions[at].none_told = true;
  portEXIT_CRITICAL(&s_lock);
  if (tell) {
    err_report(ERR_NEW(ERR_SESSION_NONE, .src = src));
  }
  err_free(sys_session_send_control(at, SYS_SESSION_NO_SESSION, NULL, 0));
  err_free(sys_session_hello_send(at));
}

/* ----------------------------------------------------------------------------
 * Answers
 * ------------------------------------------------------------------------- */

/* The answer to a command, to its source: the header and the router's data as two parts. It is the router's `done`
 * (ctx NULL: it waits for room in the provider's queue at most the driver's bound), and the RX task's answer to a
 * request it does not run (ctx: the provider's send flags, NOWAIT). */
static err_h session_answer(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  const int32_t at = sys_session_route(cmd->src);
  ERR_CHECK(at >= 0, ERR_SESSION_NO_ROUTE, .dst = cmd->src);
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_ANSWER) | (status == ROUTER_STATUS_ERROR ? SYS_SESSION_FRAME_STATUS : 0)),
      .dst = cmd->src,
      .src = s_state.board_id,
      .seq = cmd->seq,
  };
  const uint8_t urgent = (cmd->flags & ROUTER_FLAG_URGENT) ? SYS_DATA_PROVIDER_SEND_URGENT : 0;
  return sys_session_send_parts(at, &hdr, HDR, data, len, (uint8_t)((uintptr_t)ctx | urgent));
}

// A request this board does not run (refused, no route): its error record to its source, at once, never waiting.
static void answer_error(const cmd_req_t* cmd, err_h err) {
  uint8_t record[REJECT_BYTES];
  const uint32_t n = cmd_err_record(cmd, err, record, sizeof(record));
  void* const nowait = (void*)(uintptr_t)SYS_DATA_PROVIDER_SEND_NOWAIT;
  err_report(session_answer(nowait, cmd, ROUTER_STATUS_ERROR, record, n));
}

/* ----------------------------------------------------------------------------
 * What comes in (the driver's RX task)
 * ------------------------------------------------------------------------- */

static cmd_req_t request_cmd(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  return (cmd_req_t){.req = body, .req_len = len, .seq = hdr->seq, .src = hdr->src,
                     .flags = (uint8_t)(hdr->frame & SYS_SESSION_FRAME_FLAGS)};
}

/* PEER from the master: a peer of the root came or went, so its PUSH counters start again; one that went is gone here
 * (what it had here ends). One that came is the app's to follow. */
static void on_peer(int32_t at, const uint8_t* body, uint32_t len) {
  sys_session_peer_t peer;
  if (len < sizeof(peer)) {
    return;
  }
  memcpy(&peer, body, sizeof(peer));
  portENTER_CRITICAL(&s_lock);
  const bool from_master = master_at() == at;
  if (from_master && peer.id <= SYS_SESSION_ID_CLIENT_LAST) {
    counter_forget_locked(peer.id);
  }
  portEXIT_CRITICAL(&s_lock);
  if (from_master && !peer.up) {
    tell_gone(peer.id, SYS_SESSION_VIA_NOTICE);
  }
}

static void on_control(int32_t at, const uint8_t* body, uint32_t len) {
  if (len == 0) {
    return;
  }
  const session_slot_t slot = sys_session_slot(at);
  switch (body[0]) {
    case SYS_SESSION_HELLO:
    case SYS_SESSION_NO_SESSION:
      sys_session_hello_control(at, body, len);
      break;
    case SYS_SESSION_PEER:
      on_peer(at, body + 1, len - 1);
      break;
    default:  // PING: the session is alive; on one that is not open here, the peer missed its end
      if (!session_is_open(&slot)) {
        sys_session_start_over(at, SYS_SESSION_ID_NONE);
      }
      break;
  }
}

// A request for this board (or a broadcast): queued to the router, which answers through session_answer. A request
// the router does not take is answered and reported here.
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

/* A frame for another board or client, which only the root relays (between a client and a slave): on, as it is, to the
 * route's provider with NOWAIT, without the router and without keeping anything. A slave relays nothing, and the root
 * never from a slave to a slave. A request that can't go on is answered to its source; an answer or a PUSH frame is
 * dropped. */
static void forward(int32_t from, const sys_session_hdr_t* hdr, const uint8_t* frame, uint32_t len) {
  portENTER_CRITICAL(&s_lock);
  const int32_t master = master_at();
  int32_t to = from == master ? -1 : route_locked(hdr->dst);
  if (to >= 0 && sys_session_role(&s_state.sessions[from]) == SYS_SESSION_SLAVE &&
      sys_session_role(&s_state.sessions[to]) == SYS_SESSION_SLAVE) {
    to = -1;
  }
  portEXIT_CRITICAL(&s_lock);
  err_h err = to < 0 ? ERR_NEW(ERR_SESSION_NO_ROUTE, .dst = hdr->dst)
                     : sys_session_send_parts(to, NULL, 0, frame, len, sys_session_send_flags(hdr->frame));
  if (err == NULL) {
    return;
  }
  if (FRAME_KIND(hdr->frame) == SYS_SESSION_REQUEST && !(hdr->frame & ROUTER_FLAG_NOREPLY)) {
    const cmd_req_t cmd = request_cmd(hdr, frame + HDR, len - HDR);
    answer_error(&cmd, err);
  } else {
    err_free(err);
  }
}

// A broadcast request (dst 0xFF): run here and sent to every slave except `at` (a slave has none). Never answered.
static void on_broadcast(int32_t at, const sys_session_hdr_t* hdr, const uint8_t* frame, uint32_t len) {
  if (FRAME_KIND(hdr->frame) != SYS_SESSION_REQUEST) {
    return;
  }
  pass_to_router(hdr, frame + HDR, len - HDR);
  err_free(send_down(at, NULL, 0, frame, len, hdr->frame));
}

// The provider's rx (ctx: the slot): a frame, or the provider up or down.
static void on_rx(void* ctx, uint8_t id, const uint8_t* data, uint32_t len) {
  (void)id;
  const int32_t at = (int32_t)(uintptr_t)ctx;
  if (data == NULL) {
    if (len > 0) {
      if (sys_session_admits(at)) {
        err_free(sys_session_hello_send(at));  // nobody may hear it yet (a BLE client not subscribed): its hello asks again
      }
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
    portENTER_CRITICAL(&s_lock);
    s_state.sessions[at].silent_ms = 0;
    portEXIT_CRITICAL(&s_lock);
    on_control(at, body, body_len);
    return;
  }

  // Anything else on an open session only, from the peer it was opened with.
  portENTER_CRITICAL(&s_lock);
  session_slot_t* s = &s_state.sessions[at];
  // A slave's or a client's frames carry its own id; the master's any (it relays the clients' to its slaves).
  const bool ok = session_is_open(s) && (sys_session_role(s) == SYS_SESSION_MASTER || hdr.src == s->hello.id);
  if (ok) {
    s->silent_ms = 0;
  }
  portEXIT_CRITICAL(&s_lock);
  if (!ok) {
    sys_session_start_over(at, hdr.src);
    return;
  }
  if (hdr.dst == SYS_SESSION_ID_BROADCAST) {
    on_broadcast(at, &hdr, data, len);
  } else if (hdr.dst != s_state.board_id) {
    forward(at, &hdr, data, len);
  } else if (kind == SYS_SESSION_REQUEST) {
    pass_to_router(&hdr, body, body_len);
  } else if (kind == SYS_SESSION_ANSWER) {
    sys_session_call_answer(&hdr, body, body_len);
  } else {
    const uint16_t lost = push_lost(&hdr);  // DATA too: one counter for every PUSH of the pair
    if (body_len > 0 && body[0] == SYS_SESSION_CHANNEL_DATA) {
      tell_provider(&hdr, body, body_len);
    } else {
      tell_push(&hdr, lost, body, body_len);
    }
  }
}

/* ----------------------------------------------------------------------------
 * The timer (the board timers' task): session timeouts, pings, hellos on a DOWN way, call timeouts
 * ------------------------------------------------------------------------- */

static void on_tick(void* arg) {
  (void)arg;
  const uint32_t now = (uint32_t)board_time_uptime_ms();
  for (int32_t at = 0; at < CONFIG_SYS_SESSION_MAX; at++) {
    portENTER_CRITICAL(&s_lock);
    session_slot_t* s = &s_state.sessions[at];
    const bool live = session_is_open(s);
    if (live) {
      s->silent_ms = (uint16_t)(s->silent_ms + TICK_MS);
      s->ping_ms = (uint16_t)(s->ping_ms + TICK_MS);
    }
    const bool silent = live && s->silent_ms > timeout_of(s);
    // Nothing sent for a third of the peer's timeout (sys_session_send_parts zeroes it).
    const bool ping = live && s->ping_ms >= s->hello.timeout_ms / 3u;
    if (ping) {
      s->ping_ms = 0;  // a busy queue too: the next try a third later
    }
    /* A DOWN way not open says hello again every HELLO_RETRY_MS: the board on it may have started after this one's
     * hello (a board just flashed, a cable plugged in), and a board answers only the hello it gets. Not on a board with
     * a master: it opens no other session. */
    const bool waiting = s->used && !live && s->way == SYS_SESSION_DOWN && master_at() < 0;
    if (waiting) {
      s->ping_ms = (uint16_t)(s->ping_ms + TICK_MS);
    }
    const bool hello = waiting && s->ping_ms >= HELLO_RETRY_MS;
    if (hello) {
      s->ping_ms = 0;
    }
    portEXIT_CRITICAL(&s_lock);
    if (silent) {
      sys_session_end(at);
    } else if (ping) {
      err_free(sys_session_send_control(at, SYS_SESSION_PING, NULL, 0));
    } else if (hello) {
      err_free(sys_session_hello_send(at));  // a provider down refuses it: the next try a second later
    }
  }
  sys_session_call_expire(now);
}

/* ----------------------------------------------------------------------------
 * Start and this board
 * ------------------------------------------------------------------------- */

err_h sys_session_init(void) {
  s_state.board_id = sys_session_board_load_id();
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_MAX; i++) {
    s_state.sessions[i].hello.id = SYS_SESSION_ID_NONE;
  }
  counter_forget_locked(SYS_SESSION_ID_BROADCAST);  // no task runs yet
  board_timer_init(&s_state.timer, on_tick, NULL);
  board_timer_start(&s_state.timer, TICK_MS);
  return NULL;
}

uint8_t sys_session_get_board_id(void) {
  return s_state.board_id;
}

err_h sys_session_set_board_id(uint8_t board_id) {
  ERR_CHECK_IN_RANGE(board_id, 0, SYS_SESSION_ID_LAST, ERR_CMD_BAD_ARG);
  return sys_session_board_save_id(board_id);
}

err_h sys_session_set_timeout(uint32_t ms) {
  ERR_CHECK_IN_RANGE(ms, SYS_SESSION_TIMEOUT_MIN_MS, SYS_SESSION_TIMEOUT_MAX_MS, ERR_CMD_BAD_ARG);
  s_state.timeout_ms = ms;
  for (int32_t at = 0; at < CONFIG_SYS_SESSION_MAX; at++) {
    portENTER_CRITICAL(&s_lock);
    const session_slot_t* s = &s_state.sessions[at];
    const bool tell = session_is_open(s) && sys_session_role(s) != SYS_SESSION_SLAVE;
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

static void slot_free(int32_t at) {
  portENTER_CRITICAL(&s_lock);
  s_state.sessions[at].used = false;
  portEXIT_CRITICAL(&s_lock);
}

err_h sys_session_attach(uint8_t provider, uint8_t way) {
  ERR_CHECK(way == SYS_SESSION_UP || way == SYS_SESSION_DOWN, ERR_CMD_BAD_ARG);
  portENTER_CRITICAL(&s_lock);
  const int32_t attached = find_by_provider(provider);
  const bool same = attached >= 0 && s_state.sessions[attached].way == way;  // a stored record run again
  int32_t at = -1;
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_MAX && at < 0 && attached < 0; i++) {
    at = s_state.sessions[i].used ? -1 : i;
  }
  if (at >= 0) {
    s_state.sessions[at] = (session_slot_t){.used = true, .provider = provider, .way = way,
                                             .hello = {.id = SYS_SESSION_ID_NONE}};
  }
  portEXIT_CRITICAL(&s_lock);
  if (same) {
    return NULL;
  }
  ERR_CHECK(attached < 0, ERR_CMD_BAD_ARG);
  ERR_CHECK(at >= 0, ERR_SESSION_FULL);
  err_h err = sys_data_provider_open(provider, on_rx, (void*)(uintptr_t)at);
  if (err == NULL && !sys_data_provider_is_framed(provider)) {
    err_free(sys_data_provider_close(provider));
    err = ERR_NEW(ERR_SESSION_NOT_FRAMED);
  }
  if (err != NULL) {
    slot_free(at);
    return err;
  }
  if (sys_data_provider_is_up(provider)) {
    err_free(sys_session_hello_send(at));  // up already (UART): the hello goes now; else when its rx says up
  }
  return NULL;
}

err_h sys_session_detach(uint8_t provider) {
  portENTER_CRITICAL(&s_lock);
  const int32_t at = find_by_provider(provider);
  portEXIT_CRITICAL(&s_lock);
  if (at < 0) {
    return NULL;
  }
  ERR_TRY(sys_data_provider_close(provider));
  sys_session_end(at);
  slot_free(at);
  return NULL;
}
