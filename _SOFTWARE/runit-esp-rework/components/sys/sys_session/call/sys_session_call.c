#include <string.h>

#include "board_time.h"
#include "sdkconfig.h"
#include "sys_session.h"
#include "sys_session_internal.h"
#include "utils.h"

#define RECORD_BYTES   160u  // an error record made for a call's callback
#define CALL_MIN_MS    10u
#define WAIT_MARGIN_MS 50u   // a waited call's own limit past its timeout: the timer's tick and the answer's way

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
} s_call;

/* ----------------------------------------------------------------------------
 * Ending a call: taken out of its slot under the lock by whoever ends it, then its callback
 * ------------------------------------------------------------------------- */

typedef bool (*call_match_f)(const pending_call_t* c, const void* key);

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

// key: the uint16_t seq of a waited call that gave up.
static bool has_seq(const pending_call_t* c, const void* key) {
  return c->seq == *(const uint16_t*)key;
}

static cmd_req_t call_cmd(const pending_call_t* c) {
  return (cmd_req_t){.req = c->req, .req_len = c->req_len, .seq = c->seq, .src = sys_session_get_board_id()};
}

static void finish_call(const pending_call_t* c, uint8_t status, const uint8_t* data, uint32_t len) {
  if (c->cb != NULL) {
    const cmd_req_t cmd = call_cmd(c);
    err_report(c->cb(c->ctx, &cmd, status, data, len));
  }
}

// A call that ends with this board's own error (no answer, the session ended).
static void fail_call(const pending_call_t* c, err_h err) {
  uint8_t record[RECORD_BYTES];
  const cmd_req_t cmd = call_cmd(c);
  const uint32_t len = cmd_err_record(&cmd, err, record, sizeof(record));
  err_free(err);
  finish_call(c, ROUTER_STATUS_ERROR, record, len);
}

void sys_session_call_answer(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  pending_call_t call;
  if (!take_call(answered_by, hdr, &call)) {
    return;  // late: the call ended already
  }
  if (!(hdr->frame & SYS_SESSION_FRAME_STATUS)) {
    finish_call(&call, ROUTER_STATUS_OK, body, len);
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

static sys_session_hdr_t request_hdr(uint8_t dst, uint16_t seq, uint8_t flags) {
  return (sys_session_hdr_t){
      .frame = (uint8_t)(FRAME(SYS_SESSION_REQUEST) | (flags & SYS_SESSION_FRAME_FLAGS)),
      .dst = dst,
      .src = sys_session_get_board_id(),
      .seq = seq,
  };
}

/* A broadcast: a slave sends it to its master, which runs it and sends it to every slave; the root sends it to every
 * slave only, since its caller has done it already (the root's STOP and clock: run here too, a broadcast of the clock
 * would make the root set its clock, and broadcast it, for ever). */
static err_h call_broadcast(cmd_in_t req, uint8_t flags) {
  portENTER_CRITICAL(&s_lock);
  const uint16_t seq = ++s_call.seq;
  portEXIT_CRITICAL(&s_lock);
  const sys_session_hdr_t hdr = request_hdr(SYS_SESSION_ID_BROADCAST, seq, flags);
  return sys_session_send_broadcast(&hdr, req, flags);
}

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
static err_h keep_call(pending_call_t* call) {
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
  return NULL;
}

/* A call to another board; its seq and timeout come back for a waiter, which may have to end it itself. */
static err_h call_remote(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, router_done_f cb, void* ctx,
                         uint16_t* seq, uint32_t* timeout_out) {
  ERR_CHECK(!(flags & ROUTER_FLAG_NOREPLY), ERR_CMD_BAD_ARG);  // between boards every call is answered
  const int32_t via = sys_session_route(dst);
  ERR_CHECK(via >= 0, ERR_SESSION_NO_ROUTE, .dst = dst);
  const uint32_t ms = call_timeout_ms(dst, timeout_ms);
  pending_call_t call = {.used = true, .dst = dst, .via = (uint8_t)via, .cb = cb, .ctx = ctx,
                         .deadline_ms = (uint32_t)board_time_uptime_ms() + ms};
  call.req_len = (uint8_t)(req.len < sizeof(call.req) ? req.len : sizeof(call.req));
  memcpy(call.req, req.data, call.req_len);
  ERR_TRY(keep_call(&call));
  *seq = call.seq;
  *timeout_out = ms;
  const sys_session_hdr_t hdr = request_hdr(dst, call.seq, flags);
  err_h err = sys_session_send_parts(via, &hdr, HDR, req.data, req.len, sys_session_send_flags(hdr.frame));
  if (err != NULL) {
    pending_call_t dropped;
    if (!take_call(has_seq, &call.seq, &dropped)) {  // a concurrent end or expire took it and finishes waiter / cb
      err_free(err);
      return NULL;
    }
  }
  return err;
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
  uint16_t seq = 0;
  uint32_t ms = 0;
  return call_remote(dst, req, flags, timeout_ms, cb, ctx, &seq, &ms);
}

/* ----------------------------------------------------------------------------
 * Waiting for a call: a flag the callback sets, and a limit of the waiter's own
 * ------------------------------------------------------------------------- */

typedef struct {
  TaskHandle_t task;
  uint8_t* buf;
  uint32_t cap;
  uint32_t* len;
  err_h err;
  volatile bool done;  // set by the callback last: until then `w` is the callback's, after it the waiter's again
} waiter_t;

static err_h wait_done(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
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
  const TaskHandle_t task = w->task;
  __atomic_store_n(&w->done, true, __ATOMIC_RELEASE);
  xTaskNotifyGive(task);  // a notification another one ate is no harm: the waiter looks at `done`
  return NULL;
}

#define WAIT_FOREVER UINT32_MAX

/* Sleeps until the callback says done, at most `limit_ms` (WAIT_FOREVER: no limit). Another wake (an urgent
 * command's, a timer's) is no end: the flag is. */
static bool wait_flag(waiter_t* w, uint32_t limit_ms) {
  const TickType_t end = xTaskGetTickCount() + (limit_ms == WAIT_FOREVER ? 0 : MSEC(limit_ms));
  while (!__atomic_load_n(&w->done, __ATOMIC_ACQUIRE)) {
    TickType_t wait = portMAX_DELAY;
    if (limit_ms != WAIT_FOREVER) {
      const TickType_t now = xTaskGetTickCount();
      if ((int32_t)(end - now) <= 0) {
        return false;
      }
      wait = end - now;
    }
    (void)ulTaskNotifyTake(pdTRUE, wait);
  }
  return true;
}

err_h sys_session_call_wait(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, void* buf, uint32_t cap,
                            uint32_t* len) {
  ERR_CHECK(dst != SYS_SESSION_ID_BROADCAST && buf != NULL && len != NULL, ERR_CMD_BAD_ARG);
  ERR_CHECK_IN_RANGE(req.len, 2u, CMD_DATA_MAX, ERR_CMD_BAD_ARG);
  waiter_t w = {.task = xTaskGetCurrentTaskHandle(), .buf = buf, .cap = cap, .len = len};
  if (is_here(dst)) {
    ERR_CHECK(!router_is_task(), ERR_SESSION_WOULD_BLOCK);  // the router would wait for itself
    ERR_TRY(router_inject(req, flags, wait_done, &w));
    (void)wait_flag(&w, WAIT_FOREVER);  // the router always answers
    return w.err;
  }
  uint16_t seq = 0;
  uint32_t ms = 0;
  ERR_TRY(call_remote(dst, req, flags, timeout_ms, wait_done, &w, &seq, &ms));
  /* The session's timer ends the call in its time; a waiter on a task that timer or the answer needs (the board
   * timers' task, the RX task of the way) ends it itself, a margin later. */
  if (!wait_flag(&w, ms + WAIT_MARGIN_MS)) {
    pending_call_t call;
    if (take_call(has_seq, &seq, &call)) {
      return ERR_NEW(ERR_SESSION_TIMEOUT, .dst = dst, .seq = seq);  // taken out here: no callback will touch `w`
    }
    (void)wait_flag(&w, WAIT_FOREVER);  // its end is under way on another task: it writes `w`, then says done
  }
  return w.err;
}
