#include "sys_session.h"

#include <string.h>

#include "board_time.h"
#include "board_timer.h"
#include "sdkconfig.h"
#include "sys_session_internal.h"
#include "utils.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_SESSION, "sys_session", ERR_SYS_SESSION_MAP)
MODULE_ID_DEFINE(SESSION, 0x0E)

#define HDR            SYS_SESSION_HDR_BYTES
#define FRAME(kind)    SYS_SESSION_FRAME(kind)
#define FRAME_KIND(f)  SYS_SESSION_FRAME_KIND_OF(f)
#define TICK_MS        10u    // the timer: pings, session timeouts, call timeouts
#define HELLO_RETRY_MS 1000u  // a way to a slave, not open, says hello again this often
#define DATA_HEAD      3u     // [SYS_SESSION_CHANNEL_DATA][to id][from id] in front of a data frame's data
#define REJECT_BYTES   64u    // an error record answered on the RX task (its stack)

_Static_assert(CONFIG_SYS_SESSION_TIMEOUT_MS >= SYS_SESSION_TIMEOUT_MIN_MS, "CONFIG_SYS_SESSION_TIMEOUT_MS too short");
_Static_assert((ROUTER_FLAG_URGENT | ROUTER_FLAG_NOREPLY) == SYS_SESSION_FRAME_FLAGS, "the frame byte's flags");
_Static_assert(ROUTER_FLAG_URGENT == CMD_SEND_URGENT, "ROUTER_FLAG_URGENT must match CMD_SEND_URGENT");
_Static_assert(ROUTER_STATUS_ERROR == 1, "the frame byte's status bit");
_Static_assert(ROUTER_SRC_SELF == SYS_SESSION_ID_SELF, "the board's own commands");
_Static_assert(ROUTER_SRC_NONE == SYS_SESSION_ID_NONE, "a command with no peer");
_Static_assert(SYS_SESSION_TIMEOUT_MAX_MS + TICK_MS <= UINT16_MAX, "a session's silence and ping in 16 bits");

typedef struct {
  bool used;
  uint8_t provider;
  bool slave;                // told at ATTACH: the provider leads to a slave
  sys_session_hello_t peer;  // its hello; id SYS_SESSION_ID_NONE until the session is open
  uint16_t silent_ms;        // since its last frame
  uint16_t ping_ms;          // since this board's last ping (not open, a slave: last hello)
} session_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static struct {
  session_t sessions[CONFIG_SYS_SESSION_MAX];
  sys_session_hello_t me;  // this board's hello; its timeout is set for each session
  uint32_t timeout_ms;     // of the master's and the clients' sessions (the dead-man's time)
  board_timer_t timer;
  uint16_t push_seq[SYS_SESSION_ID_CLIENT_LAST + 1u][2];  // the next PUSH seq to a peer: [normal, urgent]
  sys_session_gone_f gone[CONFIG_SYS_SESSION_PEER_LISTENERS];
  sys_session_reached_f reached[CONFIG_SYS_SESSION_PEER_LISTENERS];
  sys_session_push_f push[CONFIG_SYS_SESSION_PUSH_LISTENERS];
} s_state = {.timeout_ms = CONFIG_SYS_SESSION_TIMEOUT_MS};

#define FOR_EACH_SESSION(s) for (session_t* s = s_state.sessions; s < s_state.sessions + CONFIG_SYS_SESSION_MAX; s++)

/* ----------------------------------------------------------------------------
 * The sessions' state (under s_lock)
 * ------------------------------------------------------------------------- */

static bool is_open(const session_t* s) {
  return s->used && s->peer.id != SYS_SESSION_ID_NONE;
}

static uint8_t role_of(const session_t* s) {
  if (s->slave) {
    return SYS_SESSION_SLAVE;
  }
  if (s->peer.id == SYS_SESSION_ID_NONE) {
    return SYS_SESSION_UNKNOWN;
  }
  return s->peer.id <= SYS_SESSION_ID_LAST ? SYS_SESSION_MASTER : SYS_SESSION_CLIENT;
}

static session_t* master(void) {
  FOR_EACH_SESSION(s) {
    if (is_open(s) && role_of(s) == SYS_SESSION_MASTER) {
      return s;
    }
  }
  return NULL;
}

static session_t* open_with(uint8_t peer) {
  FOR_EACH_SESSION(s) {
    if (is_open(s) && s->peer.id == peer) {
      return s;
    }
  }
  return NULL;
}

static uint16_t timeout_of(const session_t* s) {
  return (uint16_t)(s->slave ? CONFIG_SYS_SESSION_TIMEOUT_MS : s_state.timeout_ms);
}

/* ----------------------------------------------------------------------------
 * Routes and queries
 * ------------------------------------------------------------------------- */

bool sys_session_route(uint8_t dst, uint8_t* provider) {
  portENTER_CRITICAL(&s_lock);
  const session_t* s = master();
  if (s == NULL) {
    s = open_with(dst);
  }
  if (s != NULL) {
    *provider = s->provider;
  }
  portEXIT_CRITICAL(&s_lock);
  return s != NULL;
}

bool sys_session_wired(uint8_t board) {
  uint8_t provider = 0;
  return sys_session_route(board, &provider) && (sys_data_provider_state(provider) & SYS_DATA_PROVIDER_STATE_WIRED) != 0;
}

bool sys_session_is_root(void) {
  portENTER_CRITICAL(&s_lock);
  const bool root = master() == NULL;
  portEXIT_CRITICAL(&s_lock);
  return root;
}

uint8_t sys_session_get_board_id(void) {
  return s_state.me.id;
}

uint8_t sys_session_list(sys_session_info_t out[CONFIG_SYS_SESSION_MAX]) {
  uint8_t count = 0;
  portENTER_CRITICAL(&s_lock);
  FOR_EACH_SESSION(s) {
    if (s->used) {
      out[count++] = (sys_session_info_t){
          .provider = s->provider,
          .peer = role_of(s),
          .open = is_open(s),
          .peer_id = s->peer.id,
          .build_id = is_open(s) ? s->peer.build_id : 0,
          .timeout_ms = timeout_of(s),
      };
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return count;
}

/* ----------------------------------------------------------------------------
 * Frames out: every frame leaves as one flat frame, with the send flags its frame byte gives
 * ------------------------------------------------------------------------- */

// NOWAIT, URGENT by the frame byte, SPARE for a PUSH: telemetry never takes the room an answer needs.
static uint8_t send_flags(uint8_t frame) {
  const uint8_t spare = FRAME_KIND(frame) == SYS_SESSION_PUSH ? CMD_SEND_SPARE : 0u;
  return (uint8_t)(CMD_SEND_NOWAIT | (frame & CMD_SEND_URGENT) | spare);
}

// [hdr][head][body] as one frame on `provider`; `head` is a DATA frame's, else empty.
static err_h send(uint8_t provider, const sys_session_hdr_t* hdr, cmd_in_t head, cmd_in_t body, uint8_t flags) {
  ERR_CHECK(head.len + body.len <= CMD_DATA_MAX, ERR_CMD_BAD_LEN, .got = (uint16_t)(head.len + body.len),
            .need = CMD_DATA_MAX);
  uint8_t frame[HDR + head.len + body.len];
  memcpy(frame, hdr, HDR);
  if (head.len > 0) {
    memcpy(frame + HDR, head.data, head.len);
  }
  if (body.len > 0) {
    memcpy(frame + HDR + head.len, body.data, body.len);
  }
  return sys_data_provider_send(provider, (cmd_in_t){frame, sizeof(frame)}, flags);
}

err_h sys_session_send_request(uint8_t provider, uint8_t dst, uint16_t seq, cmd_in_t req, uint8_t flags) {
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_REQUEST) | (flags & ROUTER_FLAG_URGENT)),
      .dst = dst,
      .src = s_state.me.id,
      .seq = seq,
  };
  return send(provider, &hdr, (cmd_in_t){0}, req, send_flags(hdr.frame));
}

// A control frame; one a full queue refuses is lost: the hello is said again, a ping follows, a PEER is not repeated.
static void send_control(uint8_t provider, uint8_t dst, uint8_t op, const void* data, uint32_t len) {
  const sys_session_hdr_t hdr = {.frame = FRAME(SYS_SESSION_CONTROL), .dst = dst, .src = s_state.me.id};
  err_free(send(provider, &hdr, (cmd_in_t){&op, sizeof(op)}, (cmd_in_t){data, len}, CMD_SEND_NOWAIT));
}

// This board's hello on `s`; not while another way leads to its master (no other session opens then).
static void send_hello(const session_t* s) {
  sys_session_hello_t hello = s_state.me;
  portENTER_CRITICAL(&s_lock);
  const session_t* m = master();
  const bool ok = s->used && (m == NULL || m == s);
  const uint8_t provider = s->provider;
  const uint8_t dst = s->peer.id;
  hello.timeout_ms = timeout_of(s);
  portEXIT_CRITICAL(&s_lock);
  if (ok) {
    send_control(provider, dst, SYS_SESSION_HELLO, &hello, sizeof(hello));
  }
}

// A broadcast goes on to every board next to this one but the one it came from (NULL: this board made it): from a
// slave to its master, from the root to its slaves.
static err_h spread(const session_t* from, cmd_in_t frame, uint8_t flags) {
  err_h first = NULL;
  FOR_EACH_SESSION(s) {
    portENTER_CRITICAL(&s_lock);
    const bool board = s != from && is_open(s) && role_of(s) != SYS_SESSION_CLIENT;
    const uint8_t provider = s->provider;
    portEXIT_CRITICAL(&s_lock);
    if (board) {
      ERR_KEEP_FIRST(first, sys_data_provider_send(provider, frame, flags));
    }
  }
  return first;
}

err_h sys_session_broadcast(cmd_in_t req, uint8_t flags) {
  ERR_CHECK(req.data != NULL && req.len <= CMD_DATA_MAX, ERR_CMD_BAD_ARG);
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_REQUEST) | ROUTER_FLAG_NOREPLY | (flags & ROUTER_FLAG_URGENT)),
      .dst = SYS_SESSION_ID_BROADCAST,
      .src = s_state.me.id,
  };
  uint8_t frame[HDR + req.len];
  memcpy(frame, &hdr, HDR);
  memcpy(frame + HDR, req.data, req.len);
  return spread(NULL, (cmd_in_t){frame, sizeof(frame)}, send_flags(hdr.frame));
}

/* ----------------------------------------------------------------------------
 * PUSH and DATA
 * ------------------------------------------------------------------------- */

static err_h push(uint8_t peer, cmd_in_t head, cmd_in_t data, uint8_t flags) {
  ERR_CHECK(peer <= SYS_SESSION_ID_CLIENT_LAST && data.data != NULL && data.len > 0, ERR_CMD_BAD_ARG);
  uint8_t provider = 0;
  ERR_CHECK(sys_session_route(peer, &provider), ERR_SESSION_NO_ROUTE, .dst = peer);
  sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_PUSH) | (flags & ROUTER_FLAG_URGENT)),
      .dst = peer,
      .src = s_state.me.id,
  };
  portENTER_CRITICAL(&s_lock);
  hdr.seq = s_state.push_seq[peer][flags & ROUTER_FLAG_URGENT]++;
  portEXIT_CRITICAL(&s_lock);
  return send(provider, &hdr, head, data, send_flags(hdr.frame));
}

err_h sys_session_push(uint8_t peer, cmd_in_t data, uint8_t flags) {
  return push(peer, (cmd_in_t){0}, data, flags);
}

err_h sys_session_data(uint8_t peer, uint8_t to, uint8_t from, cmd_in_t data, uint8_t flags) {
  const uint8_t head[DATA_HEAD] = {SYS_SESSION_CHANNEL_DATA, to, from};
  return push(peer, (cmd_in_t){head, sizeof(head)}, data, flags);
}

/* ----------------------------------------------------------------------------
 * Listeners and the root's PEER
 * ------------------------------------------------------------------------- */

#define ADD_LISTENER(table, fn)                     \
  for (uint32_t i = 0; i < ARRAY_LEN(table); i++) { \
    if ((table)[i] == NULL) {                       \
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

// The root tells every other open session that its session with `id` opened or ended.
static void announce(const session_t* about, uint8_t id, bool up) {
  const sys_session_peer_t peer = {.id = id, .up = up ? 1u : 0u};
  FOR_EACH_SESSION(s) {
    portENTER_CRITICAL(&s_lock);
    const bool tell = s != about && is_open(s) && master() == NULL;
    const uint8_t provider = s->provider;
    const uint8_t dst = s->peer.id;
    portEXIT_CRITICAL(&s_lock);
    if (tell) {
      send_control(provider, dst, SYS_SESSION_PEER, &peer, sizeof(peer));
    }
  }
}

/* ----------------------------------------------------------------------------
 * A session's end and its hello
 * ------------------------------------------------------------------------- */

static void end_session(session_t* s) {
  portENTER_CRITICAL(&s_lock);
  const bool was_open = is_open(s);
  const uint8_t peer = s->peer.id;
  const uint8_t role = role_of(s);
  const uint8_t provider = s->provider;
  s->peer.id = SYS_SESSION_ID_NONE;
  portEXIT_CRITICAL(&s_lock);
  if (!was_open) {
    return;
  }
  sys_session_call_fail_via(provider);
  if (role == SYS_SESSION_MASTER) {
    tell_gone(SYS_SESSION_ID_BROADCAST, role);  // every peer was reached through it
    return;
  }
  tell_gone(peer, role);
  announce(s, peer, false);
}

// A frame on a session not open here: the peer says its hello again, and hears this board's.
static void start_over(const session_t* s) {
  send_control(s->provider, SYS_SESSION_ID_NONE, SYS_SESSION_NO_SESSION, NULL, 0);
  send_hello(s);
}

static void on_hello(session_t* s, const uint8_t* body, uint32_t len) {
  sys_session_hello_t hello;
  if (len < sizeof(hello)) {
    return;
  }
  memcpy(&hello, body, sizeof(hello));
  if (hello.timeout_ms < SYS_SESSION_TIMEOUT_MIN_MS) {
    hello.timeout_ms = SYS_SESSION_TIMEOUT_MIN_MS;
  }
  const bool board = hello.id <= SYS_SESSION_ID_LAST;
  const uint8_t up_role = board ? SYS_SESSION_MASTER : SYS_SESSION_CLIENT;
  const uint8_t role = s->slave ? SYS_SESSION_SLAVE : up_role;

  portENTER_CRITICAL(&s_lock);
  const session_t* m = master();
  const session_t* same = open_with(hello.id);
  const bool ok = s->used && (m == NULL || m == s);
  // An id of its kind that no other session has (the master's hello ends the others anyway).
  const bool taken = role == SYS_SESSION_CLIENT
                         ? hello.id < SYS_SESSION_ID_CLIENT_FIRST || hello.id > SYS_SESSION_ID_CLIENT_LAST
                         : !board || hello.id == s_state.me.id;
  const bool twice = role != SYS_SESSION_MASTER && same != NULL && same != s;
  const bool restarted = is_open(s) && (s->peer.boot_id != hello.boot_id || s->peer.id != hello.id);
  portEXIT_CRITICAL(&s_lock);
  if (!ok) {
    return;
  }
  if (taken || twice) {
    err_report(ERR_NEW(ERR_SESSION_ID_TAKEN, .board = hello.id));
    return;
  }
  if (restarted) {
    end_session(s);  // what it had here is gone
  }

  portENTER_CRITICAL(&s_lock);
  const bool was_open = is_open(s);
  s->peer = hello;
  s->silent_ms = 0;
  portEXIT_CRITICAL(&s_lock);
  if (role == SYS_SESSION_MASTER) {
    FOR_EACH_SESSION(other) {
      if (other != s) {
        end_session(other);  // a board with a master has no other session
      }
    }
  }
  if (!was_open) {
    for (uint32_t i = 0; i < ARRAY_LEN(s_state.reached) && s_state.reached[i] != NULL; i++) {
      s_state.reached[i](hello.id);
    }
    announce(s, hello.id, true);
  }
  // A client is answered every hello (it may have missed this board's); a board its first only, so two boards do not
  // answer each other for ever.
  if (role == SYS_SESSION_CLIENT || !was_open) {
    send_hello(s);
  }
}

/* ----------------------------------------------------------------------------
 * Answers
 * ------------------------------------------------------------------------- */

static err_h answer(const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len, uint8_t nowait) {
  uint8_t provider = 0;
  ERR_CHECK(sys_session_route(cmd->src, &provider), ERR_SESSION_NO_ROUTE, .dst = cmd->src);
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_ANSWER) | (status == ROUTER_STATUS_ERROR ? SYS_SESSION_FRAME_STATUS : 0) |
                         (cmd->flags & ROUTER_FLAG_URGENT)),
      .dst = cmd->src,
      .src = s_state.me.id,
      .seq = cmd->seq,
  };
  return send(provider, &hdr, (cmd_in_t){0}, (cmd_in_t){data, len}, (uint8_t)(nowait | (cmd->flags & CMD_SEND_URGENT)));
}

// The router's answer, on its task: waits for room in the provider's queue, at most the driver's bound.
static err_h on_answer(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  (void)ctx;
  return answer(cmd, status, data, len, 0);
}

// A request refused on the RX task, answered ERROR at once. `err` stays the caller's.
static void reject(const cmd_req_t* cmd, err_h err) {
  if (cmd->flags & ROUTER_FLAG_NOREPLY) {
    return;
  }
  uint8_t record[REJECT_BYTES];
  const uint32_t len = cmd_err_record(cmd, err, record, sizeof(record));
  err_free(answer(cmd, ROUTER_STATUS_ERROR, record, len, CMD_SEND_NOWAIT));
}

/* ----------------------------------------------------------------------------
 * What comes in (the driver's RX task)
 * ------------------------------------------------------------------------- */

static void to_router(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  cmd_req_t cmd = {.req = body, .req_len = len, .seq = hdr->seq, .src = hdr->src,
                   .flags = (uint8_t)(hdr->frame & SYS_SESSION_FRAME_FLAGS)};
  if (hdr->dst == SYS_SESSION_ID_BROADCAST) {
    cmd.flags |= ROUTER_FLAG_NOREPLY;
  }
  err_h err = router_submit(&cmd, on_answer, NULL);
  if (err != NULL) {
    reject(&cmd, err);
    router_report_cmd(&cmd, err);
  }
}

// A frame for another board: only the root relays, between a client and a slave, the frame as it is.
static void forward(const session_t* from, const sys_session_hdr_t* hdr, const uint8_t* frame, uint32_t len) {
  portENTER_CRITICAL(&s_lock);
  const session_t* to = master() == NULL ? open_with(hdr->dst) : NULL;
  if (to != NULL && role_of(from) == SYS_SESSION_SLAVE && role_of(to) == SYS_SESSION_SLAVE) {
    to = NULL;
  }
  const uint8_t provider = to != NULL ? to->provider : 0;
  portEXIT_CRITICAL(&s_lock);
  err_h err = to == NULL ? ERR_NEW(ERR_SESSION_NO_ROUTE, .dst = hdr->dst)
                         : sys_data_provider_send(provider, (cmd_in_t){frame, len}, send_flags(hdr->frame));
  if (err != NULL && FRAME_KIND(hdr->frame) == SYS_SESSION_REQUEST) {
    const cmd_req_t cmd = {.req = frame + HDR, .req_len = len - HDR, .seq = hdr->seq, .src = hdr->src,
                           .flags = (uint8_t)(hdr->frame & SYS_SESSION_FRAME_FLAGS)};
    reject(&cmd, err);
  }
  err_free(err);
}

static void on_push(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  if (len > DATA_HEAD && body[0] == SYS_SESSION_CHANNEL_DATA) {  // [DATA][to id][from id][data]: to that provider
    err_free(sys_data_provider_send(body[1], (cmd_in_t){body + DATA_HEAD, len - DATA_HEAD}, send_flags(hdr->frame)));
    return;
  }
  for (uint32_t i = 0; i < ARRAY_LEN(s_state.push) && s_state.push[i] != NULL; i++) {
    s_state.push[i](hdr->src, body, len);
  }
}

static void on_rx(void* ctx, uint8_t id, const uint8_t* data, uint32_t len) {
  (void)id;
  session_t* s = ctx;
  if (data == NULL) {  // the provider went up (len 1) or down (len 0)
    if (len > 0) {
      send_hello(s);
    } else {
      end_session(s);
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
  const uint8_t op = (kind == SYS_SESSION_CONTROL && body_len > 0) ? body[0] : 0;
  if (op == SYS_SESSION_HELLO) {
    on_hello(s, body + 1, body_len - 1);
    return;
  }
  if (op == SYS_SESSION_NO_SESSION) {
    end_session(s);
    send_hello(s);
    return;
  }

  // Anything else passes on an open session only, from its peer: a client's and a slave's frames carry its own id.
  portENTER_CRITICAL(&s_lock);
  const bool used = s->used;
  const bool from_master = is_open(s) && role_of(s) == SYS_SESSION_MASTER;
  const bool ok = from_master || (is_open(s) && hdr.src == s->peer.id);
  if (ok) {
    s->silent_ms = 0;
  }
  portEXIT_CRITICAL(&s_lock);
  if (!used) {
    return;
  }
  if (!ok) {
    start_over(s);
    return;
  }
  if (kind == SYS_SESSION_CONTROL) {  // PING: the silence is over; PEER: the master's peer went
    if (op == SYS_SESSION_PEER && from_master && body_len > sizeof(sys_session_peer_t) && body[2] == 0) {
      tell_gone(body[1], SYS_SESSION_VIA_NOTICE);
    }
  } else if (hdr.dst == SYS_SESSION_ID_BROADCAST) {
    if (kind == SYS_SESSION_REQUEST) {
      to_router(&hdr, body, body_len);
      err_free(spread(s, (cmd_in_t){data, len}, send_flags(hdr.frame)));
    }
  } else if (hdr.dst != s_state.me.id) {
    forward(s, &hdr, data, len);
  } else if (kind == SYS_SESSION_REQUEST) {
    to_router(&hdr, body, body_len);
  } else if (kind == SYS_SESSION_ANSWER) {
    sys_session_call_answer(&hdr, body, body_len);
  } else {
    on_push(&hdr, body, body_len);
  }
}

/* ----------------------------------------------------------------------------
 * The timer (the board timers' task)
 * ------------------------------------------------------------------------- */

static void on_tick(void* arg) {
  (void)arg;
  FOR_EACH_SESSION(s) {
    bool end = false;
    bool ping = false;
    bool hello = false;
    portENTER_CRITICAL(&s_lock);
    const uint8_t provider = s->provider;
    const uint8_t dst = s->peer.id;
    if (is_open(s)) {
      s->silent_ms = (uint16_t)(s->silent_ms + TICK_MS);
      s->ping_ms = (uint16_t)(s->ping_ms + TICK_MS);
      end = s->silent_ms > timeout_of(s);
      ping = !end && s->ping_ms >= s->peer.timeout_ms / 3u;
    } else if (s->used && s->slave) {
      s->ping_ms = (uint16_t)(s->ping_ms + TICK_MS);
      hello = s->ping_ms >= HELLO_RETRY_MS;  // the board on it may have started after this one
    }
    if (ping || hello) {
      s->ping_ms = 0;
    }
    portEXIT_CRITICAL(&s_lock);
    if (end) {
      end_session(s);
    } else if (ping) {
      send_control(provider, dst, SYS_SESSION_PING, NULL, 0);
    } else if (hello) {
      send_hello(s);
    }
  }
  sys_session_call_expire((uint32_t)board_time_uptime_ms());
}

/* ----------------------------------------------------------------------------
 * Start, timeout, attach, detach
 * ------------------------------------------------------------------------- */

err_h sys_session_init(void) {
  s_state.me = (sys_session_hello_t){
      .id = sys_session_board_load_id(),
      .boot_id = sys_session_board_boot_id(),
      .build_id = sys_session_board_build_id(),
  };
  FOR_EACH_SESSION(s) {
    s->peer.id = SYS_SESSION_ID_NONE;
  }
  board_timer_init(&s_state.timer, on_tick, NULL);
  board_timer_start(&s_state.timer, TICK_MS);
  return NULL;
}

err_h sys_session_set_timeout(uint32_t ms) {
  ERR_CHECK_IN_RANGE(ms, SYS_SESSION_TIMEOUT_MIN_MS, SYS_SESSION_TIMEOUT_MAX_MS, ERR_CMD_BAD_ARG);
  s_state.timeout_ms = ms;
  FOR_EACH_SESSION(s) {
    portENTER_CRITICAL(&s_lock);
    const bool tell = is_open(s) && !s->slave;
    portEXIT_CRITICAL(&s_lock);
    if (tell) {
      send_hello(s);  // the peer sends at least every third of the new one
    }
  }
  return NULL;
}

err_h sys_session_attach(uint8_t provider, uint8_t peer) {
  ERR_CHECK(peer == SYS_SESSION_UNKNOWN || peer == SYS_SESSION_SLAVE, ERR_CMD_BAD_ARG);
  const bool slave = peer == SYS_SESSION_SLAVE;
  session_t* at = NULL;
  const session_t* attached = NULL;
  portENTER_CRITICAL(&s_lock);
  FOR_EACH_SESSION(s) {
    if (s->used && s->provider == provider) {
      attached = s;
    } else if (!s->used && at == NULL) {
      at = s;
    }
  }
  const bool same = attached != NULL && attached->slave == slave;
  if (attached == NULL && at != NULL) {
    *at = (session_t){.used = true, .provider = provider, .slave = slave, .peer = {.id = SYS_SESSION_ID_NONE}};
  }
  portEXIT_CRITICAL(&s_lock);
  if (same) {
    return NULL;  // a stored record that attaches runs at every start
  }
  ERR_CHECK(attached == NULL, ERR_CMD_BAD_ARG);
  ERR_CHECK(at != NULL, ERR_SESSION_FULL);
  err_h err = sys_data_provider_open(provider, on_rx, at);
  const uint8_t state = sys_data_provider_state(provider);
  if (err == NULL && (state & SYS_DATA_PROVIDER_STATE_FRAMED) == 0) {
    err_free(sys_data_provider_close(provider));
    err = ERR_NEW(ERR_SESSION_NOT_FRAMED);
  }
  if (err != NULL) {
    at->used = false;
    return err;
  }
  if (state & SYS_DATA_PROVIDER_STATE_UP) {
    send_hello(at);
  }
  return NULL;
}

err_h sys_session_detach(uint8_t provider) {
  FOR_EACH_SESSION(s) {
    if (s->used && s->provider == provider) {
      ERR_TRY(sys_data_provider_close(provider));
      end_session(s);
      s->used = false;
      return NULL;
    }
  }
  return NULL;
}
