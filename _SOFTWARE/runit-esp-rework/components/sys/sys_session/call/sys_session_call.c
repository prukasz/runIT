#include <string.h>

#include "board_time.h"
#include "err_record.h"
#include "sdkconfig.h"
#include "sys_session.h"
#include "sys_session_internal.h"
#include "utils.h"

#define RECORD_BYTES   160u  // an error record made for a call's callback
#define WAIT_MARGIN_MS 50u   // a waited call's own limit past its timeout: the timer's tick and the answer's way

/* A call to another board waiting for its answer. */
typedef struct {
  bool used;
  uint8_t dst;
  uint8_t via;      // the provider it went out on
  uint8_t req_len;  // bytes of `req` kept
  uint16_t seq;
  uint32_t deadline_ms;  // board_time_uptime_ms() as 32 bits (compared by the difference) when the timer fails it
  router_done_f cb;
  void* ctx;
  uint8_t req[CONFIG_ERRORS_RECORD_REQ_MAX];
} call_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static struct {
  call_t calls[CONFIG_SYS_SESSION_CALLS];
  uint16_t seq;
} s_call;

/* ----------------------------------------------------------------------------
 * Ending a call: taken out of its slot under the lock by whoever ends it, then its callback
 * ------------------------------------------------------------------------- */

typedef enum {
  BY_SEQ,  // key: its seq (the answer, a waiter that gave up, a request that did not go out)
  BY_VIA,  // key: the provider it went out on (its session ended)
  BY_DUE,  // key: the time now (its deadline passed)
} take_e;

static bool take_call(take_e by, uint32_t key, call_t* out) {
  bool found = false;
  portENTER_CRITICAL(&s_lock);
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_CALLS && !found; i++) {
    call_t* c = &s_call.calls[i];
    found = c->used && (by == BY_SEQ   ? c->seq == key
                        : by == BY_VIA ? c->via == key
                                       : (int32_t)(key - c->deadline_ms) >= 0);
    if (found) {
      *out = *c;
      c->used = false;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return found;
}

// Ends a call: OK with the answer's data, or ERROR with this board's record of `err` (freed here).
static void finish_call(const call_t* c, uint8_t status, err_h err, const uint8_t* data, uint32_t len) {
  const cmd_req_t cmd = {.req = c->req, .req_len = c->req_len, .seq = c->seq, .src = sys_session_get_board_id()};
  uint8_t record[RECORD_BYTES];
  if (status == ROUTER_STATUS_ERROR) {
    data = record;
    len = err_record_cmd(&cmd, err, record, sizeof(record));
  }
  if (c->cb != NULL) {
    err_report(c->cb(c->ctx, &cmd, status, data, len));
  }
  err_free(err);
}

void sys_session_call_answer(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  call_t call;
  if (!take_call(BY_SEQ, hdr->seq, &call)) {
    return;  // late: the call ended already
  }
  if (!(hdr->frame & SYS_SESSION_FRAME_STATUS)) {
    finish_call(&call, ROUTER_STATUS_OK, NULL, body, len);
    return;
  }
  // That board's chain under ERR_REMOTE, so the error policy's walk stops at it.
  err_h chain = NULL;
  err_free(err_record_parse(body, len, &chain));
  finish_call(&call, ROUTER_STATUS_ERROR, ERR_WRAP(chain, ERR_REMOTE, .board = call.dst, .seq = hdr->seq), NULL, 0);
}

void sys_session_call_fail_via(uint8_t provider) {
  call_t call;
  while (take_call(BY_VIA, provider, &call)) {
    finish_call(&call, ROUTER_STATUS_ERROR, ERR_NEW(ERR_SESSION_PEER_DOWN, .board = call.dst), NULL, 0);
  }
}

void sys_session_call_expire(uint32_t now_ms) {
  call_t call;
  while (take_call(BY_DUE, now_ms, &call)) {
    finish_call(&call, ROUTER_STATUS_ERROR, ERR_NEW(ERR_SESSION_TIMEOUT, .dst = call.dst, .seq = call.seq), NULL, 0);
  }
}

/* ----------------------------------------------------------------------------
 * Making a call
 * ------------------------------------------------------------------------- */

static bool is_here(uint8_t dst) {
  return dst == sys_session_get_board_id() || dst == SYS_SESSION_ID_SELF;
}

static uint32_t timeout_or_default(uint32_t timeout_ms) {
  return timeout_ms > 0 ? timeout_ms : CONFIG_SYS_SESSION_CALL_TIMEOUT_MS;
}

/* A call to another board: kept in a slot under a new seq, then sent. `seq` comes back for a waiter, which may have
 * to end the call itself. */
static err_h call_remote(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, router_done_f cb, void* ctx,
                         uint16_t* seq) {
  ERR_CHECK(req.data != NULL, ERR_CMD_BAD_ARG);
  uint8_t via = 0;
  ERR_CHECK(sys_session_route(dst, &via), ERR_SESSION_NO_ROUTE, .dst = dst);
  call_t call = {.used = true, .dst = dst, .via = via, .cb = cb, .ctx = ctx,
                 .deadline_ms = (uint32_t)board_time_uptime_ms() + timeout_ms};
  call.req_len = (uint8_t)(req.len < sizeof(call.req) ? req.len : sizeof(call.req));
  memcpy(call.req, req.data, call.req_len);
  bool kept = false;
  portENTER_CRITICAL(&s_lock);
  for (int32_t i = 0; i < CONFIG_SYS_SESSION_CALLS && !kept; i++) {
    if (!s_call.calls[i].used) {
      call.seq = ++s_call.seq;
      s_call.calls[i] = call;
      kept = true;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(kept, ERR_SESSION_CALLS_FULL);
  *seq = call.seq;
  err_h err = sys_session_send_request(via, call.seq, req, flags);
  if (err != NULL && !take_call(BY_SEQ, call.seq, &call)) {  // its session ended meanwhile: the callback has the end
    err_free(err);
    return NULL;
  }
  return err;
}

err_h sys_session_call(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, router_done_f cb, void* ctx) {
  if (is_here(dst)) {
    return router_inject(req, flags, cb, ctx);
  }
  uint16_t seq = 0;
  return call_remote(dst, req, flags, timeout_or_default(timeout_ms), cb, ctx, &seq);
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
  if (status != ROUTER_STATUS_OK) {
    err_free(err_record_parse(data, len, &w->err));
    if (w->err == NULL) {
      w->err = ERR_NEW(ERR_CMD_BAD_ARG);
    }
  } else if (len > w->cap) {
    w->err = ERR_NEW(ERR_CMD_BAD_LEN, .got = (uint16_t)len, .need = (uint16_t)w->cap);
  } else {
    memcpy(w->buf, data, len);
    *w->len = len;
  }
  const TaskHandle_t task = w->task;
  __atomic_store_n(&w->done, true, __ATOMIC_RELEASE);
  xTaskNotifyGive(task);  // a notification another one ate is no harm: the waiter looks at `done`
  return NULL;
}

/* Sleeps until the callback says done, at most `limit` ticks. Another wake (an urgent command's, a timer's) is no
 * end: the flag is. */
static bool wait_flag(const waiter_t* w, TickType_t limit) {
  const TickType_t start = xTaskGetTickCount();
  while (!__atomic_load_n(&w->done, __ATOMIC_ACQUIRE)) {
    const TickType_t spent = xTaskGetTickCount() - start;
    if (spent >= limit) {
      return false;
    }
    (void)ulTaskNotifyTake(pdTRUE, limit - spent);
  }
  return true;
}

err_h sys_session_call_wait(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, void* buf, uint32_t cap,
                            uint32_t* len) {
  waiter_t w = {.task = xTaskGetCurrentTaskHandle(), .buf = buf, .cap = cap, .len = len};
  if (is_here(dst)) {
    ERR_CHECK(!router_is_task(), ERR_SESSION_WOULD_BLOCK);  // the router would wait for itself
    ERR_TRY(router_inject(req, flags, wait_done, &w));
  } else {
    /* The session's timer ends the call in its time; a waiter on a task that timer or the answer needs (the board
     * timers' task, the RX task of the way) ends it itself, a margin later. */
    const uint32_t ms = timeout_or_default(timeout_ms);
    uint16_t seq = 0;
    call_t call;
    ERR_TRY(call_remote(dst, req, flags, ms, wait_done, &w, &seq));
    if (!wait_flag(&w, MSEC(ms + WAIT_MARGIN_MS)) && take_call(BY_SEQ, seq, &call)) {
      return ERR_NEW(ERR_SESSION_TIMEOUT, .dst = dst, .seq = seq);  // taken out here: no callback will touch `w`
    }
  }
  (void)wait_flag(&w, portMAX_DELAY);  // the router always answers; a call's end under way on another task says done
  return w.err;
}
