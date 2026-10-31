#include "esp_log.h"
#include "router.h"
#include "router_priv.h"
#include "sdkconfig.h"

#define TAG __FILE_NAME__

#define REQ_MIN_BYTES 2u  // a root key and the direction

#define NOTIFY_CMD    (1u << 0)  // urgent task: a command in the urgent ring
#define NOTIFY_URGENT (1u << 1)  // urgent task: run the fn of router_run_urgent

_Static_assert(CONFIG_ROUTER_WORK_QUEUE_SIZE % 4 == 0, "ROUTER_WORK_QUEUE_SIZE must be a multiple of 4");
_Static_assert(CONFIG_ROUTER_URGENT_QUEUE_SIZE % 4 == 0, "ROUTER_URGENT_QUEUE_SIZE must be a multiple of 4");

U_RINGBUFFER_DEFINE(router_work_rb, CONFIG_ROUTER_WORK_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_RINGBUFFER_DEFINE(router_urgent_rb, CONFIG_ROUTER_URGENT_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(router_work_task_h, CONFIG_ROUTER_WORK_TASK_STACK);
U_TASK_DEFINE(router_urgent_task_h, CONFIG_ROUTER_URGENT_TASK_STACK);

// A queued command: this header, then its request.
typedef struct {
  router_done_f done;
  void* ctx;
  uint8_t link;
  uint8_t seq;
  uint8_t flags;
} item_t;

static struct {
  const router_tap_t* tap;
  void (*urgent_fn)(void);  // router_run_urgent: the fn waiting to run, NULL when none
  uint8_t inject_seq;       // seq of the board's own commands (diagnostics only)
  // One answer buffer per task (the two run at the same time): CMD_HEAD bytes for the done's header, then the data.
  uint8_t buf_work[CMD_FRAME_MAX];
  uint8_t buf_urgent[CMD_FRAME_MAX];
} s_dispatch;

static __thread const err_cmd_t* s_route;  // per router task: the command it is routing, NULL between commands

/* ----------------------------------------------------------------------------
 * Reporting
 * ------------------------------------------------------------------------- */

/* Ends a command's chain: logged with the command ("cmd link 1 seq 7 flags 00
 * req 02 02 00") and handed to the error sink with the command as origin. */
void router_report_cmd(const err_cmd_t* cmd, err_h err) {
  char ctx[48 + 3 * CONFIG_ERRORS_RECORD_REQ_MAX + 4];
  int pos = snprintf(ctx, sizeof(ctx), "cmd link %u seq %u flags %02x req", cmd->link, cmd->seq, cmd->flags);
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
 * Queue: router_submit and router_inject
 * ------------------------------------------------------------------------- */

static void notify_urgent(uint32_t bits) {
  if (router_urgent_task_h != NULL) {
    xTaskNotify(router_urgent_task_h, bits, eSetBits);
  }
}

err_h router_submit(const err_cmd_t* cmd, router_done_f done, void* ctx) {
  ERR_CHECK(cmd != NULL && (cmd->req != NULL || cmd->req_len == 0), ERR_CMD_BAD_ARG);
  ERR_CHECK(cmd->req_len >= REQ_MIN_BYTES && cmd->req_len <= CMD_DATA_MAX, ERR_ROUTER_FRAME_LEN, .link = cmd->link,
            .len = (uint16_t)cmd->req_len, .max = CMD_DATA_MAX);

  const bool urgent = (cmd->flags & ROUTER_FLAG_URGENT) != 0;
  RingbufHandle_t rb = urgent ? router_urgent_rb : router_work_rb;
  void* slot = NULL;
  ERR_CHECK(xRingbufferSendAcquire(rb, &slot, sizeof(item_t) + cmd->req_len, 0) == pdTRUE, ERR_ROUTER_QUEUE_FULL,
            .link = cmd->link, .urgent = urgent);
  const item_t item = {.done = done, .ctx = ctx, .link = cmd->link, .seq = cmd->seq, .flags = cmd->flags};
  memcpy(slot, &item, sizeof(item));
  memcpy((uint8_t*)slot + sizeof(item), cmd->req, cmd->req_len);
  xRingbufferSendComplete(rb, slot);
  if (urgent) {
    notify_urgent(NOTIFY_CMD);
  }
  return NULL;
}

err_h router_inject(cmd_in_t req, uint8_t flags, router_done_f done, void* ctx) {
  ERR_CHECK(req.data != NULL && req.len > 0, ERR_CMD_BAD_ARG);
  const err_cmd_t cmd = {
      .req = req.data,
      .req_len = req.len,
      .link = ROUTER_LINK_INTERNAL,
      .seq = __atomic_fetch_add(&s_dispatch.inject_seq, 1, __ATOMIC_RELAXED),
      .flags = flags,
  };
  return router_submit(&cmd, done, ctx);
}

/* ----------------------------------------------------------------------------
 * Tap, origin, urgent work
 * ------------------------------------------------------------------------- */

void router_set_tap(const router_tap_t* tap) {
  s_dispatch.tap = tap;
}

const err_cmd_t* router_origin(void) {
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
  notify_urgent(NOTIFY_URGENT);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Tasks
 * ------------------------------------------------------------------------- */

// Routes one queued command, hands its answer to its done and reports what failed.
static void process(const uint8_t* slot, size_t size, uint8_t* buf) {
  item_t item;
  memcpy(&item, slot, sizeof(item));
  const err_cmd_t cmd = {
      .req = slot + sizeof(item),
      .req_len = (uint32_t)(size - sizeof(item)),
      .link = item.link,
      .seq = item.seq,
      .flags = item.flags,
  };
  cmd_out_t out = {.data = buf + CMD_HEAD, .cap = CMD_DATA_MAX};
  err_h err = NULL;
  const router_tap_t* tap = s_dispatch.tap;
  if (tap == NULL || tap->take == NULL || !tap->take(&cmd)) {
    s_route = &cmd;
    err = router_route((cmd_in_t){cmd.req, cmd.req_len}, &out);
    s_route = NULL;
    if (tap != NULL && tap->seen != NULL) {
      tap->seen(&cmd, err);
    }
  }
  if (item.done != NULL && !(item.flags & ROUTER_FLAG_NOREPLY)) {
    uint32_t len = err != NULL ? err_record(&cmd, err, out.data, out.cap) : out.len;
    uint8_t status = err != NULL ? ROUTER_STATUS_ERROR : ROUTER_STATUS_OK;
    err_h lost = item.done(item.ctx, &cmd, status, out.data, len);
    if (lost != NULL) {
      router_report_cmd(&cmd, lost);
    }
  }
  if (err != NULL) {
    router_report_cmd(&cmd, err);
  }
}

static void work_task(void* arg) {
  (void)arg;
  while (1) {
    size_t size = 0;
    uint8_t* slot = xRingbufferReceive(router_work_rb, &size, WAIT_FOREVER);
    if (slot != NULL) {
      process(slot, size, s_dispatch.buf_work);
      vRingbufferReturnItem(router_work_rb, slot);
    }
  }
}

static void urgent_task(void* arg) {
  (void)arg;
  while (1) {
    // Drain first: commands may have been queued before the task started.
    size_t size = 0;
    uint8_t* slot;
    while ((slot = xRingbufferReceive(router_urgent_rb, &size, NO_WAIT)) != NULL) {
      process(slot, size, s_dispatch.buf_urgent);
      vRingbufferReturnItem(router_urgent_rb, slot);
    }
    void (*fn)(void) = __atomic_exchange_n(&s_dispatch.urgent_fn, NULL, __ATOMIC_ACQ_REL);
    if (fn != NULL) {
      fn();
    }

    uint32_t bits = 0;
    U_NOTIFY_AWAIT(WAIT_FOREVER, &bits);
  }
}

err_h router_init(void) {
  U_TASK_START(router_urgent_task_h, urgent_task, NULL, CONFIG_ROUTER_URGENT_TASK_PRIO);
  U_TASK_START(router_work_task_h, work_task, NULL, CONFIG_ROUTER_WORK_TASK_PRIO);
  return NULL;
}
