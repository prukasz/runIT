#include "sys_session_call.h"

#include <string.h>

#include "board_time.h"
#include "sdkconfig.h"
#include "sys_session.h"
#include "sys_session_internal.h"
#include "utils.h"

#define RECORD_BYTES  160u  // an error record made for a call's callback
#define NO_WAIT_TASKS 6u    // the RX tasks of the drivers, the timer's task and whoever called sys_session_no_wait
#define CALL_MIN_MS   10u

/* A call to another board waiting for its answer. */
typedef struct {
  bool used;
  uint8_t dst;
  uint8_t via;      // the session it went through
  uint8_t req_len;  // bytes of `req` kept
  uint16_t seq;
  uint32_t deadline_ms;  // board_time_uptime_ms() as 32 bits (compared by the difference) when the timer fails it
  router_done_f cb;
  void* ctx;
  uint8_t req[CONFIG_ERRORS_RECORD_REQ_MAX];
} pending_call_t;

/* The calls' own lock: never held with the sessions' (a call takes its route first, then its slot). */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static struct {
  pending_call_t calls[CONFIG_SYS_SESSION_CALLS];
  uint16_t seq;
  TaskHandle_t no_wait[NO_WAIT_TASKS];
} s_call;

/* ----------------------------------------------------------------------------
 * Ending a call
 * ------------------------------------------------------------------------- */

typedef bool (*call_match_f)(const pending_call_t* c, const void* key);

/* Takes the first call `match` accepts out of its slot, which is free again from here: false when there is none. */
static bool take_call(call_match_f match, const void* key, pending_call_t* out) {
  bool found = false;
  portENTER_CRITICAL(&s_lock);
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_CALLS && !found; i++) {
    if (s_call.calls[i].used && match(&s_call.calls[i], key)) {
      *out = s_call.calls[i];
      s_call.calls[i].used = false;
      found = true;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return found;
}

// key: the answer's header. The call it answers: its seq, to its src.
static bool answered_by(const pending_call_t* c, const void* key) {
  const sys_session_hdr_t* hdr = key;
  return c->seq == hdr->seq && c->dst == hdr->src;
}

// key: an int32_t session. A call that went through it.
static bool went_via(const pending_call_t* c, const void* key) {
  return c->via == *(const int32_t*)key;
}

// key: the uint32_t time now. A call whose deadline passed.
static bool is_due(const pending_call_t* c, const void* key) {
  return (int32_t)(*(const uint32_t*)key - c->deadline_ms) >= 0;
}

static cmd_req_t call_cmd(const pending_call_t* c) {
  return (cmd_req_t){.req = c->req, .req_len = c->req_len, .seq = c->seq, .src = sys_session_get_board_id()};
}

static void finish_call(const pending_call_t* c, uint8_t status, uint8_t* data, uint32_t len) {
  if (c->cb != NULL) {
    const cmd_req_t cmd = call_cmd(c);
    err_report(c->cb(c->ctx, &cmd, status, data, len));
  }
}

// A call that ends with this board's own error (no answer, the session ended).
static void fail_call(const pending_call_t* c, err_h err) {
  uint8_t buf[CMD_HEAD + RECORD_BYTES];
  const cmd_req_t cmd = call_cmd(c);
  const uint32_t len = cmd_err_record(&cmd, err, buf + CMD_HEAD, RECORD_BYTES);
  err_free(err);
  finish_call(c, ROUTER_STATUS_ERROR, buf + CMD_HEAD, len);
}

void sys_session_call_answer(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  pending_call_t call;
  if (!take_call(answered_by, hdr, &call)) {
    return;  // late: the call ended already
  }
  if (!(hdr->frame & SYS_SESSION_FRAME_STATUS)) {
    // The receive buffer has the frame's header in front of the body: the room the callback may write in.
    finish_call(&call, ROUTER_STATUS_OK, (uint8_t*)(uintptr_t)body, len);
    return;
  }
  // That board's chain under ERR_REMOTE, so the error policy's walk stops at it.
  err_h chain = NULL;
  err_free(err_record_parse(body, len, &chain));
  chain = ERR_WRAP(chain, ERR_REMOTE, .board = hdr->src, .seq = hdr->seq);
  fail_call(&call, chain);
}

void sys_session_call_fail_via(int32_t at) {
  pending_call_t call;
  while (take_call(went_via, &at, &call)) {
    fail_call(&call, ERR_NEW(ERR_SESSION_PEER_DOWN, .board = call.dst));
  }
}

void sys_session_call_expire(uint32_t now_ms) {
  pending_call_t call;
  while (take_call(is_due, &now_ms, &call)) {
    fail_call(&call, ERR_NEW(ERR_SESSION_TIMEOUT, .dst = call.dst, .seq = call.seq));
  }
}

/* ----------------------------------------------------------------------------
 * Making a call
 * ------------------------------------------------------------------------- */

/* The request of a call as a frame in `buf` (CMD_FRAME_MAX): the header, then the command. */
static uint32_t call_frame(uint8_t* buf, uint8_t dst, uint16_t seq, uint8_t flags, cmd_in_t req) {
  const sys_session_hdr_t hdr = {
      .frame = (uint8_t)(FRAME(SYS_SESSION_REQUEST) | (flags & SYS_SESSION_FRAME_FLAGS)),
      .dst = dst,
      .src = sys_session_get_board_id(),
      .seq = seq,
  };
  memcpy(buf, &hdr, HDR);
  memcpy(buf + HDR, req.data, req.len);
  return HDR + req.len;
}

/* A broadcast: a slave sends it to its master, which runs it and sends it to every slave; the master sends it to every
 * slave only, since its caller has done it already (the master's STOP and clock: run here too, a broadcast of the clock
 * would make the master set its clock, and broadcast it, for ever). */
static err_h call_broadcast(cmd_in_t req, uint8_t flags) {
  uint8_t buf[CMD_FRAME_MAX];
  portENTER_CRITICAL(&s_lock);
  const uint16_t seq = ++s_call.seq;
  portEXIT_CRITICAL(&s_lock);
  const uint32_t len = call_frame(buf, SYS_SESSION_ID_BROADCAST, seq, flags, req);
  return sys_session_send_broadcast(buf, len, flags);
}

// Whether `dst` is this board: its id or SYS_SESSION_ID_SELF.
static bool is_here(uint8_t dst) {
  return dst == sys_session_get_board_id() || dst == SYS_SESSION_ID_SELF;
}

// A call's timeout: the caller's, else the way's own (a WIRED neighbour's or the default); at least CALL_MIN_MS.
static uint32_t call_timeout_ms(uint8_t dst, uint32_t timeout_ms) {
  uint32_t ms = timeout_ms;
  if (ms == 0) {
    ms = sys_session_wired(dst) ? CONFIG_SYS_SESSION_CALL_TIMEOUT_WIRED_MS : CONFIG_SYS_SESSION_CALL_TIMEOUT_MS;
  }
  return ms < CALL_MIN_MS ? CALL_MIN_MS : ms;
}

/* Keeps `call` in a free slot under a new seq (set in `call`); ERR_SESSION_BUSY when its board has
 * CONFIG_SYS_SESSION_PEER_IN_FLIGHT calls already, ERR_SESSION_CALLS_FULL. */
static err_h keep_call(pending_call_t* call, int32_t* slot) {
  int32_t free_at = -1;
  uint32_t in_flight = 0;
  portENTER_CRITICAL(&s_lock);
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_CALLS; i++) {
    if (!s_call.calls[i].used) {
      free_at = free_at < 0 ? i : free_at;
    } else if (s_call.calls[i].dst == call->dst) {
      in_flight++;
    }
  }
  const bool busy = in_flight >= CONFIG_SYS_SESSION_PEER_IN_FLIGHT;
  if (free_at >= 0 && !busy) {
    call->seq = ++s_call.seq;
    s_call.calls[free_at] = *call;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(!busy, ERR_SESSION_BUSY, .dst = call->dst);
  ERR_CHECK(free_at >= 0, ERR_SESSION_CALLS_FULL);
  *slot = free_at;
  return NULL;
}

// A kept call whose request did not go out: its slot is free again, unless it has ended already.
static void drop_call(int32_t slot, uint16_t seq) {
  portENTER_CRITICAL(&s_lock);
  if (s_call.calls[slot].used && s_call.calls[slot].seq == seq) {
    s_call.calls[slot].used = false;
  }
  portEXIT_CRITICAL(&s_lock);
}

err_h sys_session_call(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, router_done_f cb, void* ctx) {
  ERR_CHECK_IN_RANGE(req.len, 2u, CMD_DATA_MAX, ERR_CMD_BAD_ARG);
  if (dst == SYS_SESSION_ID_BROADCAST) {
    ERR_CHECK((flags & ROUTER_FLAG_NOREPLY) && cb == NULL, ERR_CMD_BAD_ARG);
    return call_broadcast(req, flags);
  }
  if (is_here(dst)) {
    return router_inject(req, flags, cb, ctx);
  }
  ERR_CHECK(!(flags & ROUTER_FLAG_NOREPLY), ERR_CMD_BAD_ARG);  // between boards every call is answered
  const int32_t via = sys_session_route(dst);
  ERR_CHECK(via >= 0, ERR_SESSION_NO_ROUTE, .dst = dst);
  pending_call_t call = {.used = true, .dst = dst, .via = (uint8_t)via, .cb = cb, .ctx = ctx,
                         .deadline_ms = (uint32_t)board_time_uptime_ms() + call_timeout_ms(dst, timeout_ms)};
  call.req_len = (uint8_t)(req.len < sizeof(call.req) ? req.len : sizeof(call.req));
  memcpy(call.req, req.data, call.req_len);
  int32_t slot = -1;
  ERR_TRY(keep_call(&call, &slot));
  uint8_t buf[CMD_FRAME_MAX];
  const uint32_t len = call_frame(buf, dst, call.seq, flags, req);
  err_h err = sys_session_send_raw(via, buf, len, sys_session_send_flags(flags));
  if (err != NULL) {
    drop_call(slot, call.seq);
  }
  return err;
}

/* ----------------------------------------------------------------------------
 * Waiting for a call
 * ------------------------------------------------------------------------- */

static bool no_wait_task(void) {
  const TaskHandle_t me = xTaskGetCurrentTaskHandle();
  bool found = false;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < NO_WAIT_TASKS; i++) {
    found |= s_call.no_wait[i] == me;
  }
  portEXIT_CRITICAL(&s_lock);
  return found;
}

void sys_session_no_wait(void) {
  const TaskHandle_t me = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < NO_WAIT_TASKS; i++) {
    if (s_call.no_wait[i] == me) {
      break;
    }
    if (s_call.no_wait[i] == NULL) {
      s_call.no_wait[i] = me;
      break;
    }
  }
  portEXIT_CRITICAL(&s_lock);
}

typedef struct {
  TaskHandle_t task;
  uint8_t* buf;
  uint32_t cap;
  uint32_t* len;
  err_h err;
} waiter_t;

// The answer of a waited call: into the caller's buffer, then the caller wakes.
static err_h wait_done(void* ctx, const cmd_req_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  (void)cmd;
  waiter_t* w = ctx;
  if (status == ROUTER_STATUS_OK) {
    if (len > w->cap) {
      w->err = ERR_NEW(ERR_CMD_BAD_LEN, .got = (uint16_t)len, .need = (uint16_t)w->cap);
    } else {
      memcpy(w->buf, data, len);
      *w->len = len;
    }
  } else {
    err_h chain = NULL;
    err_free(err_record_parse(data, len, &chain));
    w->err = chain != NULL ? chain : ERR_NEW(ERR_CMD_BAD_ARG);
  }
  xTaskNotifyGive(w->task);  // `w` is the caller's again from here
  return NULL;
}

err_h sys_session_call_wait(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, void* buf, uint32_t cap,
                            uint32_t* len) {
  ERR_CHECK(dst != SYS_SESSION_ID_BROADCAST && buf != NULL && len != NULL, ERR_CMD_BAD_ARG);
  ERR_CHECK(!no_wait_task() && !(is_here(dst) && router_is_task()), ERR_SESSION_WOULD_BLOCK);
  waiter_t w = {.task = xTaskGetCurrentTaskHandle(), .buf = buf, .cap = cap, .len = len};
  (void)ulTaskNotifyTake(pdTRUE, 0);  // a stale notification must not end the wait at once
  ERR_TRY(sys_session_call(dst, req, flags, timeout_ms, wait_done, &w));
  (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // the call always ends: an answer, its timeout, its session
  return w.err;
}
