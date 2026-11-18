#include "sys_session.h"

#include <string.h>

#include "board_time.h"
#include "board_timer.h"
#include "sdkconfig.h"
#include "sys_event.h"
#include "sys_session_events.h"
#include "sys_session_internal.h"
#include "utils.h"

ERR_MODULE_DEFINE(MODULE_ID_SESSION, ERR_SYS_SESSION_MAP)

#define HDR            SYS_SESSION_HDR_BYTES
#define FRAME(kind)    SYS_SESSION_FRAME(kind)
#define FRAME_KIND(f)  SYS_SESSION_FRAME_KIND_OF(f)
#define TICK_MS        10u    // the timer: pings, session timeouts, call timeouts
#define HELLO_RETRY_MS 1000u  // a session not open says hello again this often
#define REJECT_BYTES   64u    // an error record answered on the RX task (its stack)

_Static_assert((ROUTER_FLAG_URGENT | ROUTER_FLAG_NOREPLY) == SYS_SESSION_FRAME_FLAGS, "the frame byte's flags");
_Static_assert(ROUTER_FLAG_URGENT == CMD_SEND_URGENT, "ROUTER_FLAG_URGENT must match CMD_SEND_URGENT");
_Static_assert(ROUTER_STATUS_ERROR == 1, "the frame byte's status bit");
_Static_assert(ROUTER_SRC_SELF == SYS_SESSION_ID_SELF, "the board's own commands");
_Static_assert(ROUTER_SRC_NONE == SYS_SESSION_ID_NONE, "a command with no peer");
_Static_assert(SYS_SESSION_DEADMAN_MAX_MS + TICK_MS <= UINT16_MAX, "a session's silence and ping in 16 bits");

typedef struct {
  bool used;
  bool safety;               // its end is a safety event (sys_system's safe state)
  uint8_t provider;
  uint16_t deadman_ms;       // the silence that ends it, 0: never; a third of it the keepalive
  sys_session_hello_t peer;  // its hello; id SYS_SESSION_ID_NONE until the session is open
  uint16_t silent_ms;        // since its last frame
  uint16_t ping_ms;          // since this board's last ping (not open: its last hello)
  uint16_t push_seq[2];      // the next PUSH seq to its peer: [normal, urgent]
} session_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static struct {
  session_t sessions[CONFIG_SYS_SESSION_MAX];
  sys_session_hello_t me;  // this board's hello; its timeout is set for each session
  board_timer_t timer;
  sys_session_push_f push;  // the PUSH frames' taker (the data matrix)
} s_state;

#define FOR_EACH_SESSION(s) for (session_t* s = s_state.sessions; s < s_state.sessions + CONFIG_SYS_SESSION_MAX; s++)

/* ----------------------------------------------------------------------------
 * The sessions' state (under s_lock)
 * ------------------------------------------------------------------------- */

static bool is_open(const session_t* s) {
  return s->used && s->peer.id != SYS_SESSION_ID_NONE;
}

static session_t* open_with(uint8_t peer) {
  FOR_EACH_SESSION(s) {
    if (is_open(s) && s->peer.id == peer) {
      return s;
    }
  }
  return NULL;
}

static session_t* open_on(uint8_t provider) {
  FOR_EACH_SESSION(s) {
    if (is_open(s) && s->provider == provider) {
      return s;
    }
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Queries
 * ------------------------------------------------------------------------- */

bool sys_session_route(uint8_t peer, uint8_t* provider) {
  portENTER_CRITICAL(&s_lock);
  const session_t* s = open_with(peer);
  if (s != NULL) {
    *provider = s->provider;
  }
  portEXIT_CRITICAL(&s_lock);
  return s != NULL;
}

uint8_t sys_session_peer(uint8_t provider) {
  portENTER_CRITICAL(&s_lock);
  const session_t* s = open_on(provider);
  const uint8_t peer = s != NULL ? s->peer.id : SYS_SESSION_ID_NONE;
  portEXIT_CRITICAL(&s_lock);
  return peer;
}

bool sys_session_wired(uint8_t board) {
  uint8_t provider = 0;
  return sys_session_route(board, &provider) && (sys_data_provider_state(provider) & SYS_DATA_PROVIDER_STATE_WIRED) != 0;
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
          .open = is_open(s),
          .peer_id = s->peer.id,
          .safety = s->safety,
          .build_id = is_open(s) ? s->peer.build_id : 0,
          .deadman_ms = s->deadman_ms,
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

// [hdr][head][body] as one frame on `provider`; `head` is a control frame's op, else empty.
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

err_h sys_session_send_request(uint8_t provider, uint16_t seq, cmd_in_t req, uint8_t flags) {
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_REQUEST) | (flags & ROUTER_FLAG_URGENT)),
      .seq = seq,
  };
  return send(provider, &hdr, (cmd_in_t){0}, req, send_flags(hdr.frame));
}

// A control frame; one a full queue refuses is lost: the hello is said again, a ping follows.
static void send_control(uint8_t provider, uint8_t op, const void* data, uint32_t len) {
  const sys_session_hdr_t hdr = {.frame = FRAME(SYS_SESSION_CONTROL)};
  err_free(send(provider, &hdr, (cmd_in_t){&op, sizeof(op)}, (cmd_in_t){data, len}, CMD_SEND_NOWAIT));
}

// This board's hello on `s`.
static void send_hello(const session_t* s) {
  portENTER_CRITICAL(&s_lock);
  const bool used = s->used;
  const uint8_t provider = s->provider;
  portEXIT_CRITICAL(&s_lock);
  if (used) {
    send_control(provider, SYS_SESSION_HELLO, &s_state.me, sizeof(s_state.me));
  }
}

err_h sys_session_broadcast(cmd_in_t req, uint8_t flags) {
  ERR_CHECK(req.data != NULL && req.len <= CMD_DATA_MAX, ERR_CMD_BAD_ARG);
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_REQUEST) | ROUTER_FLAG_NOREPLY | (flags & ROUTER_FLAG_URGENT)),
  };
  uint8_t frame[HDR + req.len];
  memcpy(frame, &hdr, HDR);
  memcpy(frame + HDR, req.data, req.len);
  err_h first = NULL;
  FOR_EACH_SESSION(s) {
    portENTER_CRITICAL(&s_lock);
    const bool open = is_open(s);
    const uint8_t provider = s->provider;
    portEXIT_CRITICAL(&s_lock);
    if (open) {
      ERR_KEEP_FIRST(first, sys_data_provider_send(provider, (cmd_in_t){frame, sizeof(frame)}, send_flags(hdr.frame)));
    }
  }
  return first;
}

/* ----------------------------------------------------------------------------
 * PUSH
 * ------------------------------------------------------------------------- */

err_h sys_session_push(uint8_t provider, cmd_in_t data, uint8_t flags) {
  ERR_CHECK(data.data != NULL && data.len > 0, ERR_CMD_BAD_ARG);
  const uint8_t urgent = flags & ROUTER_FLAG_URGENT;
  uint16_t seq = 0;
  portENTER_CRITICAL(&s_lock);
  session_t* s = open_on(provider);
  if (s != NULL) {
    seq = s->push_seq[urgent]++;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(s != NULL, ERR_SESSION_NOT_OPEN, .provider = provider);
  const sys_session_hdr_t hdr = {.frame = (uint8_t)(FRAME(SYS_SESSION_PUSH) | urgent), .seq = seq};
  return send(provider, &hdr, (cmd_in_t){0}, data, send_flags(hdr.frame));
}

void sys_session_set_push(sys_session_push_f fn) {
  s_state.push = fn;
}

/* ----------------------------------------------------------------------------
 * A session's end and its hello
 * ------------------------------------------------------------------------- */

static void end_session(session_t* s) {
  portENTER_CRITICAL(&s_lock);
  const bool was_open = is_open(s);
  const uint8_t peer = s->peer.id;
  const uint8_t safety = s->safety ? 1u : 0u;
  const uint8_t provider = s->provider;
  s->peer.id = SYS_SESSION_ID_NONE;
  s->silent_ms = 0;
  portEXIT_CRITICAL(&s_lock);
  if (!was_open) {
    return;
  }
  sys_session_call_fail_via(provider);
  err_report(SYS_EVENT_PUBLISH(SESSION_EV_GONE, peer, 0, .safety = safety));
}

// A frame on a session not open here: the peer says its hello again, and hears this board's.
static void start_over(const session_t* s) {
  send_control(s->provider, SYS_SESSION_NO_SESSION, NULL, 0);
  send_hello(s);
}

static void on_hello(session_t* s, const uint8_t* body, uint32_t len) {
  sys_session_hello_t hello;
  if (len < sizeof(hello)) {
    return;
  }
  memcpy(&hello, body, sizeof(hello));
  const bool board = hello.id <= SYS_SESSION_ID_LAST;
  const bool client = hello.id >= SYS_SESSION_ID_CLIENT_FIRST && hello.id <= SYS_SESSION_ID_CLIENT_LAST;

  portENTER_CRITICAL(&s_lock);
  const session_t* same = open_with(hello.id);
  const bool bad = !(board || client) || hello.id == s_state.me.id;  // an id nobody may say, or this board's own
  const bool twice = same != NULL && same != s;                      // another open session has it
  const bool restarted = is_open(s) && (s->peer.boot_id != hello.boot_id || s->peer.id != hello.id);
  const bool used = s->used;
  portEXIT_CRITICAL(&s_lock);
  if (!used) {
    return;
  }
  if (bad || twice) {
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
  if (!was_open) {
    err_report(SYS_EVENT_PUBLISH(SESSION_EV_REACHED, hello.id, 0));
  }
  // A client is answered every hello (it may have missed this board's); a board its first only, so two boards do not
  // answer each other for ever.
  if (client || !was_open) {
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

static void to_router(uint8_t src, const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  cmd_req_t cmd = {.req = body, .req_len = len, .seq = hdr->seq, .src = src,
                   .flags = (uint8_t)(hdr->frame & SYS_SESSION_FRAME_FLAGS)};
  err_h err = router_submit(&cmd, on_answer, NULL);
  if (err != NULL) {
    reject(&cmd, err);
    router_report_cmd(&cmd, err);
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

  // Anything else passes on an open session only.
  portENTER_CRITICAL(&s_lock);
  const bool used = s->used;
  const bool open = is_open(s);
  const uint8_t src = s->peer.id;
  if (open) {
    s->silent_ms = 0;
  }
  portEXIT_CRITICAL(&s_lock);
  if (!used) {
    return;
  }
  if (!open) {
    start_over(s);
    return;
  }
  if (kind == SYS_SESSION_REQUEST) {
    to_router(src, &hdr, body, body_len);
  } else if (kind == SYS_SESSION_ANSWER) {
    sys_session_call_answer(&hdr, body, body_len);
  } else if (kind == SYS_SESSION_PUSH) {
    if (s_state.push != NULL) {
      s_state.push(s->provider, body, body_len);
    }
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
    if (is_open(s)) {
      s->silent_ms = (uint16_t)(s->silent_ms + TICK_MS);
      s->ping_ms = (uint16_t)(s->ping_ms + TICK_MS);
      end = s->deadman_ms > 0 && s->silent_ms > s->deadman_ms;
      ping = s->deadman_ms > 0 && !end && s->ping_ms >= s->deadman_ms / 3u;
    } else if (s->used) {
      s->ping_ms = (uint16_t)(s->ping_ms + TICK_MS);
      hello = s->ping_ms >= HELLO_RETRY_MS;  // the peer on it may have started after this board
    }
    if (ping || hello) {
      s->ping_ms = 0;
    }
    portEXIT_CRITICAL(&s_lock);
    if (end) {
      end_session(s);
    } else if (ping) {
      send_control(provider, SYS_SESSION_PING, NULL, 0);
    } else if (hello) {
      send_hello(s);
    }
  }
  sys_session_call_expire((uint32_t)board_time_uptime_ms());
}

/* ----------------------------------------------------------------------------
 * Start, attach, detach, dead-man
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

static bool deadman_ok(uint32_t ms) {
  return ms == 0 || (ms >= SYS_SESSION_DEADMAN_MIN_MS && ms <= SYS_SESSION_DEADMAN_MAX_MS);
}

err_h sys_session_attach(uint8_t provider, uint32_t deadman_ms, uint8_t flags) {
  ERR_CHECK(deadman_ok(deadman_ms) && (flags & ~SYS_SESSION_SAFETY) == 0u, ERR_CMD_BAD_ARG);
  const bool safety = (flags & SYS_SESSION_SAFETY) != 0u;
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
  const bool same = attached != NULL && attached->safety == safety && attached->deadman_ms == deadman_ms;
  if (attached == NULL && at != NULL) {
    *at = (session_t){.used = true,
                      .safety = safety,
                      .provider = provider,
                      .deadman_ms = (uint16_t)deadman_ms,
                      .peer = {.id = SYS_SESSION_ID_NONE}};
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

err_h sys_session_deadman_set(uint8_t peer, uint32_t ms, bool safety) {
  ERR_CHECK(deadman_ok(ms), ERR_CMD_BAD_ARG);
  portENTER_CRITICAL(&s_lock);
  session_t* s = open_with(peer);
  if (s != NULL) {
    s->safety = safety && ms > 0;
    s->deadman_ms = (uint16_t)ms;
    s->silent_ms = 0;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(s != NULL, ERR_SESSION_NO_ROUTE, .dst = peer);
  return NULL;
}

uint32_t sys_session_deadman_get(uint8_t peer) {
  portENTER_CRITICAL(&s_lock);
  const session_t* s = open_with(peer);
  const uint32_t ms = s != NULL ? s->deadman_ms : 0u;
  portEXIT_CRITICAL(&s_lock);
  return ms;
}
