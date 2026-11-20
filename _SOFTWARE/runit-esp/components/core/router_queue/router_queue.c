#include "router_queue.h"
#include "err_record.h"
#include "err_sink.h"
#include "router_queue_internal.h"
#include "sdkconfig.h"

ERR_MODULE_DEFINE(MODULE_ID_ROUTER_QUEUE, ERR_ROUTER_QUEUE_MAP)

#define REQ_MIN_BYTES 2u                   // a root key and the direction
#define REQ_MAX_BYTES (CMD_DATA_MAX + 1u)  // a frame's body and the key of its header

_Static_assert(CONFIG_ROUTER_WORK_QUEUE_SIZE % 4 == 0, "ROUTER_WORK_QUEUE_SIZE must be a multiple of 4");
_Static_assert(CONFIG_ROUTER_URGENT_QUEUE_SIZE % 4 == 0, "ROUTER_URGENT_QUEUE_SIZE must be a multiple of 4");
_Static_assert(ERR_RECORD_MAX <= CMD_DATA_MAX, "an error record must fit an answer");

U_RINGBUFFER_DEFINE(router_work_rb, CONFIG_ROUTER_WORK_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_RINGBUFFER_DEFINE(router_urgent_rb, CONFIG_ROUTER_URGENT_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(router_work_task_h, CONFIG_ROUTER_WORK_TASK_STACK);
U_TASK_DEFINE(router_urgent_task_h, CONFIG_ROUTER_URGENT_TASK_STACK);

// A queued command: this header, then its request [key][dir]... Its answer goes to `done`, else to the port `port`.
typedef struct {
  router_done_f done;
  void* ctx;
  uint16_t seq;
  uint8_t src;
  uint8_t flags;
  data_matrix_port_id_t port;  // the port it came from (done NULL); 0: none
} item_t;

static struct {
  void (*urgent_fn)(void);  // router_run_urgent: the fn waiting to run, NULL when none
  uint16_t inject_seq;      // seq of the board's own commands
  // One answer buffer per task (the two run at the same time): the answer's header, then its data.
  uint8_t buf_work[CMD_FRAME_MAX];
  uint8_t buf_urgent[CMD_FRAME_MAX];
} s_dispatch;

static __thread const cmd_req_t* s_route;  // per router task: the command it is routing, NULL between commands

static void port_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
                    uint32_t len);

data_matrix_port_t router_port = {.id = ROUTER_PORT, .in = port_in};

/* ----------------------------------------------------------------------------
 * Reporting
 * ------------------------------------------------------------------------- */

// Ends a command's chain: logged with the command ("cmd src f0 seq 7 flags 00 req 02 02 00") and handed to the error
// sink with it (its record on the errors stream).
static void report_cmd(const cmd_req_t* cmd, err_h err) {
  char ctx[48 + 3 * CONFIG_ERRORS_RECORD_REQ_MAX + 4];
  int pos = snprintf(ctx, sizeof(ctx), "cmd src %02x seq %u flags %02x req", cmd->src, cmd->seq, cmd->flags);
  uint32_t shown = cmd->req_len < CONFIG_ERRORS_RECORD_REQ_MAX ? cmd->req_len : CONFIG_ERRORS_RECORD_REQ_MAX;
  for (uint32_t i = 0; i < shown && pos > 0 && (size_t)pos < sizeof(ctx); i++) {
    pos += snprintf(ctx + pos, sizeof(ctx) - pos, " %02x", cmd->req[i]);
  }
  if (shown < cmd->req_len && pos > 0 && (size_t)pos < sizeof(ctx)) {
    snprintf(ctx + pos, sizeof(ctx) - pos, " ...");
  }
  err_report_ex(err, ctx, cmd);
}

/* ----------------------------------------------------------------------------
 * Queue: router_inject
 * ------------------------------------------------------------------------- */

// Wakes the urgent task: a command in its ring, or a fn to run.
static void wake_urgent(void) {
  xTaskNotifyGive(router_urgent_task_h);
}

// Copies a command into the ring of its flags: its request, after `key` when it came in a frame (key NULL: in req).
static err_h enqueue(const item_t* item, const uint8_t* key, const uint8_t* req, uint32_t req_len) {
  const uint32_t head = key != NULL ? 1u : 0u;
  ERR_CHECK_IN_RANGE(head + req_len, REQ_MIN_BYTES, REQ_MAX_BYTES, ERR_ROUTER_FRAME_LEN, .src = item->src,
                     .len = (uint16_t)(head + req_len), .max = REQ_MAX_BYTES);
  const bool urgent = (item->flags & ROUTER_FLAG_URGENT) != 0;
  RingbufHandle_t rb = urgent ? router_urgent_rb : router_work_rb;
  void* slot = NULL;
  ERR_CHECK(xRingbufferSendAcquire(rb, &slot, sizeof(*item) + head + req_len, 0) == pdTRUE, ERR_ROUTER_QUEUE_FULL,
            .src = item->src, .urgent = urgent);
  uint8_t* at = slot;
  memcpy(at, item, sizeof(*item));
  if (key != NULL) {
    at[sizeof(*item)] = *key;
  }
  memcpy(at + sizeof(*item) + head, req, req_len);
  xRingbufferSendComplete(rb, slot);
  if (urgent) {
    wake_urgent();
  }
  return NULL;
}

err_h router_inject(cmd_in_t req, uint8_t flags, router_done_f done, void* ctx) {
  ERR_CHECK(req.data != NULL, ERR_CMD_BAD_ARG);
  const item_t item = {
      .done = done,
      .ctx = ctx,
      .seq = __atomic_fetch_add(&s_dispatch.inject_seq, 1, __ATOMIC_RELAXED),
      .src = CMD_SRC_SELF,
      .flags = flags,
  };
  return enqueue(&item, NULL, req.data, req.len);
}

/* ----------------------------------------------------------------------------
 * The port: frames in, answers to the port a request came from
 * ------------------------------------------------------------------------- */

// An answer's header at `at`: the request's key and seq, its status, its URGENT; src for the session to set.
static void answer_head(uint8_t* at, uint8_t key, uint16_t seq, uint8_t status, uint8_t flags) {
  const cmd_hdr_t hdr = {
      .type = (uint8_t)(CMD_TYPE(CMD_ANSWER) | (status == ROUTER_STATUS_ERROR ? CMD_TYPE_STATUS : 0u) |
                        (flags & ROUTER_FLAG_URGENT)),
      .src = CMD_SRC_NONE,
      .key = key,
      .seq = seq,
  };
  memcpy(at, &hdr, CMD_HEAD);
}

// A request the queue refused: answered ERROR at once (NOWAIT; not a NOREPLY one) and reported. On the sender's task.
static void reject(data_matrix_port_id_t to, const cmd_hdr_t* hdr, const uint8_t* body, uint32_t len, err_h err) {
  uint8_t req[CONFIG_ERRORS_RECORD_REQ_MAX];  // [key][body], as much as a record keeps
  const uint32_t n = len < sizeof(req) - 1u ? len : (uint32_t)sizeof(req) - 1u;
  req[0] = hdr->key;
  memcpy(req + 1, body, n);
  const uint8_t flags = hdr->type & CMD_TYPE_FLAGS;
  const cmd_req_t cmd = {.req = req, .req_len = n + 1u, .seq = hdr->seq, .src = hdr->src, .flags = flags};
  if (!(flags & ROUTER_FLAG_NOREPLY)) {
    uint8_t frame[CMD_HEAD + ERR_RECORD_MAX];
    const uint32_t rec = err_record_cmd(&cmd, err, frame + CMD_HEAD, ERR_RECORD_MAX);
    answer_head(frame, hdr->key, hdr->seq, ROUTER_STATUS_ERROR, flags);
    (void)data_matrix_send_to(&router_port, to, CMD_SEND_NOWAIT | (flags & CMD_SEND_URGENT), frame, CMD_HEAD + rec);
  }
  report_cmd(&cmd, err);
}

// A frame for the router: a REQUEST is queued, an ANSWER ends this board's call; anything else is not the router's.
static void port_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
                    uint32_t len) {
  (void)self;
  (void)flags;
  if (len < CMD_HEAD) {
    return;
  }
  cmd_hdr_t hdr;
  memcpy(&hdr, data, CMD_HEAD);
  const uint8_t kind = CMD_KIND_OF(hdr.type);
  if (kind == CMD_ANSWER) {
    router_request_answer(from, &hdr, data + CMD_HEAD, len - CMD_HEAD);
  } else if (kind == CMD_REQUEST) {
    const item_t item = {.seq = hdr.seq, .src = hdr.src, .flags = hdr.type & CMD_TYPE_FLAGS, .port = from};
    err_h err = enqueue(&item, &hdr.key, data + CMD_HEAD, len - CMD_HEAD);
    if (err != NULL) {
      reject(from, &hdr, data + CMD_HEAD, len - CMD_HEAD, err);
    }
  }
}

/* ----------------------------------------------------------------------------
 * Origin, urgent work
 * ------------------------------------------------------------------------- */

const cmd_req_t* router_origin(void) {
  return s_route;
}

bool router_is_task(void) {
  TaskHandle_t self = xTaskGetCurrentTaskHandle();
  return self == router_work_task_h || self == router_urgent_task_h;
}

err_h router_run_urgent(void (*fn)(void)) {
  ERR_CHECK(fn != NULL, ERR_CMD_BAD_ARG);
  void (*none)(void) = NULL;
  if (!__atomic_compare_exchange_n(&s_dispatch.urgent_fn, &none, fn, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    ERR_CHECK(none == fn, ERR_ROUTER_BUSY);  // the same fn already waits: it runs once
  }
  wake_urgent();
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Tasks
 * ------------------------------------------------------------------------- */

// Routes one queued command, hands its answer to its done or its port and reports what failed. `buf` is CMD_FRAME_MAX:
// the answer's header, then its data.
static void process(const uint8_t* slot, size_t size, uint8_t* buf) {
  item_t item;
  memcpy(&item, slot, sizeof(item));
  const cmd_req_t cmd = {
      .req = slot + sizeof(item),
      .req_len = (uint32_t)(size - sizeof(item)),
      .seq = item.seq,
      .src = item.src,
      .flags = item.flags,
  };
  cmd_out_t out = {.data = buf + CMD_HEAD, .cap = CMD_DATA_MAX};
  s_route = &cmd;
  err_h err = router_route((cmd_in_t){cmd.req, cmd.req_len}, &out);
  s_route = NULL;
  if ((item.done != NULL || item.port != 0) && !(item.flags & ROUTER_FLAG_NOREPLY)) {
    uint32_t len = err != NULL ? err_record_cmd(&cmd, err, out.data, ERR_RECORD_MAX) : out.len;
    uint8_t status = err != NULL ? ROUTER_STATUS_ERROR : ROUTER_STATUS_OK;
    if (item.done != NULL) {
      err_h lost = item.done(item.ctx, &cmd, status, out.data, len);
      if (lost != NULL) {
        report_cmd(&cmd, lost);
      }
    } else {  // waits for room on the way back, at most the provider's bound; what is not taken is lost
      answer_head(buf, cmd.req[0], cmd.seq, status, item.flags);
      (void)data_matrix_send_to(&router_port, item.port, item.flags & CMD_SEND_URGENT, buf, CMD_HEAD + len);
    }
  }
  if (err != NULL) {
    report_cmd(&cmd, err);
  }
}

static void work_task(void* arg) {
  (void)arg;
  while (true) {
    size_t size = 0;
    uint8_t* slot = xRingbufferReceive(router_work_rb, &size, portMAX_DELAY);
    if (slot != NULL) {
      process(slot, size, s_dispatch.buf_work);
      vRingbufferReturnItem(router_work_rb, slot);
    }
  }
}

/* Drains its ring and runs the waiting fn, then sleeps until woken. Draining first takes what was queued before the
 * task started; a wake between the drain and the wait leaves its notification pending, so the wait returns at once. */
static void urgent_task(void* arg) {
  (void)arg;
  while (true) {
    size_t size = 0;
    uint8_t* slot = xRingbufferReceive(router_urgent_rb, &size, NO_WAIT);
    while (slot != NULL) {
      process(slot, size, s_dispatch.buf_urgent);
      vRingbufferReturnItem(router_urgent_rb, slot);
      slot = xRingbufferReceive(router_urgent_rb, &size, NO_WAIT);
    }
    void (*fn)(void) = __atomic_exchange_n(&s_dispatch.urgent_fn, NULL, __ATOMIC_ACQ_REL);
    if (fn != NULL) {
      fn();
    }
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
}

// The router tasks: static (stack and TCB in U_TASK_DEFINE), so they start before app_main, after the rings above.
__attribute__((constructor)) static void tasks_start(void) {
  U_TASK_START(router_urgent_task_h, urgent_task, NULL, CONFIG_ROUTER_URGENT_TASK_PRIO);
  U_TASK_START(router_work_task_h, work_task, NULL, CONFIG_ROUTER_WORK_TASK_PRIO);
}

err_h router_init(void) {
  ERR_TRY(router_root_init());
  ERR_TRY(data_matrix_register(&router_port));
  router_request_init();
  return NULL;
}
