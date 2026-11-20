/**
 * @file router_request.c
 * @brief This board's calls (router_queue.h, ROUTER_QUEUE.MD "Calls"): a command out of the router's port to the port
 * its caller names, its answer to a callback, its timeout, the wait.
 */

#include <string.h>

#include "board_timer.h"
#include "err_record.h"
#include "router_queue.h"
#include "router_queue_internal.h"
#include "sdkconfig.h"

#define TICK_MS        10u   // the timer: the calls' timeouts
#define WAIT_MARGIN_MS 50u   // a waited call's own limit past its timeout: the timer's tick and the answer's way

/* A call out of the router's port, waiting for its answer. */
typedef struct {
  bool used;
  uint8_t req_len;  // bytes of `req` kept
  uint16_t seq;
  data_matrix_port_id_t to;
  TickType_t deadline;  // the tick count (compared by the difference) when the timer fails it
  router_done_f cb;
  void* ctx;
  uint8_t req[CONFIG_ERRORS_RECORD_REQ_MAX];
} call_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static struct {
  call_t calls[CONFIG_ROUTER_CALLS];
  uint16_t seq;
  board_timer_t timer;
} s_call;

/* ----------------------------------------------------------------------------
 * Ending a call: taken out of its slot under the lock by whoever ends it, then its callback
 * ------------------------------------------------------------------------- */

typedef enum {
  BY_SEQ,  // its seq and port (the answer, a waiter that gave up, a request that did not go out)
  BY_TO,   // its port (the link behind it ended)
  BY_DUE,  // the tick count now (its deadline passed)
} take_e;

static bool take_call(take_e by, data_matrix_port_id_t to, uint32_t key, call_t* out) {
  bool found = false;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < CONFIG_ROUTER_CALLS && !found; i++) {
    call_t* c = &s_call.calls[i];
    found = c->used && (by == BY_SEQ  ? c->seq == key && c->to == to
                        : by == BY_TO ? c->to == to
                                      : (int32_t)(key - c->deadline) >= 0);
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
  const cmd_req_t cmd = {.req = c->req, .req_len = c->req_len, .seq = c->seq, .src = CMD_SRC_SELF};
  uint8_t record[ERR_RECORD_MAX];
  if (status == ROUTER_STATUS_ERROR) {
    data = record;
    len = err_record_cmd(&cmd, err, record, sizeof(record));
  }
  if (c->cb != NULL) {
    err_report(c->cb(c->ctx, &cmd, status, data, len));
  }
  err_free(err);
}

void router_request_answer(data_matrix_port_id_t from, const cmd_hdr_t* hdr, const uint8_t* body, uint32_t len) {
  call_t call;
  if (!take_call(BY_SEQ, from, hdr->seq, &call)) {
    return;  // late: the call ended already
  }
  if (!(hdr->type & CMD_TYPE_STATUS)) {
    finish_call(&call, ROUTER_STATUS_OK, NULL, body, len);
    return;
  }
  // That board's chain under ERR_REMOTE, so the error policy's walk stops at it.
  err_h chain = NULL;
  err_free(err_record_parse(body, len, &chain));
  finish_call(&call, ROUTER_STATUS_ERROR, ERR_WRAP(chain, ERR_REMOTE, .board = hdr->src, .seq = hdr->seq), NULL, 0);
}

void router_request_fail_to(data_matrix_port_id_t to) {
  call_t call;
  while (take_call(BY_TO, to, 0, &call)) {
    finish_call(&call, ROUTER_STATUS_ERROR, ERR_NEW(ERR_ROUTER_CALL_DOWN, .port = to), NULL, 0);
  }
}

static void on_tick(void* arg) {
  (void)arg;
  call_t call;
  while (take_call(BY_DUE, 0, (uint32_t)xTaskGetTickCount(), &call)) {
    finish_call(&call, ROUTER_STATUS_ERROR, ERR_NEW(ERR_ROUTER_CALL_TIMEOUT, .port = call.to, .seq = call.seq), NULL,
                0);
  }
}

void router_request_init(void) {
  board_timer_init(&s_call.timer, on_tick, NULL);
  board_timer_start(&s_call.timer, TICK_MS);
}

/* ----------------------------------------------------------------------------
 * Making a call
 * ------------------------------------------------------------------------- */

static uint32_t timeout_or_default(uint32_t timeout_ms) {
  return timeout_ms > 0 ? timeout_ms : CONFIG_ROUTER_CALL_TIMEOUT_MS;
}

/* A call out of the port: kept in a slot under a new seq, then sent as [hdr REQUEST][req after its key]. `seq` comes
 * back for a waiter, which may have to end the call itself. */
static err_h call_out(data_matrix_port_id_t to, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, router_done_f cb,
                      void* ctx, uint16_t* seq) {
  ERR_CHECK(req.data != NULL && req.len >= 2u && req.len <= CMD_DATA_MAX + 1u, ERR_CMD_BAD_ARG);
  call_t call = {.used = true, .to = to, .cb = cb, .ctx = ctx,
                 .deadline = xTaskGetTickCount() + MSEC(timeout_ms)};
  call.req_len = (uint8_t)(req.len < sizeof(call.req) ? req.len : sizeof(call.req));
  memcpy(call.req, req.data, call.req_len);
  bool kept = false;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < CONFIG_ROUTER_CALLS && !kept; i++) {
    if (!s_call.calls[i].used) {
      call.seq = ++s_call.seq;
      s_call.calls[i] = call;
      kept = true;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(kept, ERR_ROUTER_CALLS_FULL);
  *seq = call.seq;

  uint8_t frame[CMD_HEAD + req.len - 1u];
  const cmd_hdr_t hdr = {
      .type = (uint8_t)(CMD_TYPE(CMD_REQUEST) | (flags & ROUTER_FLAG_URGENT)),
      .src = CMD_SRC_NONE,
      .key = req.data[0],
      .seq = call.seq,
  };
  memcpy(frame, &hdr, CMD_HEAD);
  memcpy(frame + CMD_HEAD, req.data + 1, req.len - 1u);
  if (data_matrix_send_to(&router_port, to, CMD_SEND_NOWAIT | (flags & CMD_SEND_URGENT), frame, sizeof(frame)) ||
      !take_call(BY_SEQ, to, call.seq, &call)) {
    return NULL;  // sent, or its link ended meanwhile: the callback has the end
  }
  return ERR_NEW(ERR_ROUTER_NO_PORT, .port = to);
}

err_h router_request(data_matrix_port_id_t to, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, router_done_f cb,
                     void* ctx) {
  uint16_t seq = 0;
  return call_out(to, req, flags, timeout_or_default(timeout_ms), cb, ctx, &seq);
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

err_h router_request_wait(data_matrix_port_id_t to, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, void* buf,
                          uint32_t cap, uint32_t* len) {
  /* The timer ends the call in its time; a waiter on a task that timer or the answer needs (the board timers' task, the
   * RX task of the link) ends it itself, a margin later. */
  waiter_t w = {.task = xTaskGetCurrentTaskHandle(), .buf = buf, .cap = cap, .len = len};
  const uint32_t ms = timeout_or_default(timeout_ms);
  uint16_t seq = 0;
  call_t call;
  ERR_TRY(call_out(to, req, flags, ms, wait_done, &w, &seq));
  if (!wait_flag(&w, MSEC(ms + WAIT_MARGIN_MS)) && take_call(BY_SEQ, to, seq, &call)) {
    return ERR_NEW(ERR_ROUTER_CALL_TIMEOUT, .port = to, .seq = seq);  // taken out here: no callback touches `w`
  }
  (void)wait_flag(&w, portMAX_DELAY);  // a call's end under way on another task says done
  return w.err;
}
