#include "esp_log.h"
#include "router.h"
#include "sdkconfig.h"

#define TAG __FILE_NAME__

#define REQ_HDR_BYTES 2u                    // [seq][flags]
#define REQ_MIN_BYTES (REQ_HDR_BYTES + 1u)  // ... and a root key
#define RESP_HDR_BYTES 3u                   // [stream][seq][status]
#define REJECT_RESP_BYTES 64u               // error answers sent from router_submit (link task stack)

#define NOTIFY_FRAME (1u << 0)  // urgent task: a frame in the urgent ring
#define NOTIFY_WATCH (1u << 1)  // urgent task: run the watch's `urgent`

_Static_assert(CONFIG_ROUTER_WORK_QUEUE_SIZE % 4 == 0, "ROUTER_WORK_QUEUE_SIZE must be a multiple of 4");
_Static_assert(CONFIG_ROUTER_URGENT_QUEUE_SIZE % 4 == 0, "ROUTER_URGENT_QUEUE_SIZE must be a multiple of 4");

// What a router task is routing: the command, its link and that task's response buffer (router_answer_part).
typedef struct {
  const err_cmd_t* cmd;
  const router_link_t* link;
  uint8_t* resp;
} route_t;

U_RINGBUFFER_DEFINE(router_work_rb, CONFIG_ROUTER_WORK_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_RINGBUFFER_DEFINE(router_urgent_rb, CONFIG_ROUTER_URGENT_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(router_work_task_h, CONFIG_ROUTER_WORK_TASK_STACK);
U_TASK_DEFINE(router_urgent_task_h, CONFIG_ROUTER_URGENT_TASK_STACK);

// Link 0: frames from the board itself (router_inject). No send: answers go nowhere.
static const router_link_t k_internal_link = {.name = "internal"};

static struct {
  const router_link_t* links[CONFIG_ROUTER_MAX_LINKS + 1];  // index = link ID, 0 unused; written at boot
  const router_tap_t* tap;
  const router_watch_t* watch;
  uint8_t inject_seq;  // seq of injected frames (diagnostics only)
  // One response buffer per task: the two tasks build answers at the same time.
  uint8_t resp_work[CONFIG_ROUTER_FRAME_MAX];
  uint8_t resp_urgent[CONFIG_ROUTER_FRAME_MAX];
} s_dispatch;

static __thread const route_t* s_route;  // per router task: what it is routing, NULL between frames

/* ----------------------------------------------------------------------------
 * Links
 * ------------------------------------------------------------------------- */

err_h router_link_register(uint8_t link_id, const router_link_t* link) {
  ERR_CHECK(link != NULL && link->send != NULL, ERR_CMD_BAD_ARG);
  ERR_CHECK(link_id > 0 && link_id <= CONFIG_ROUTER_MAX_LINKS, ERR_ROUTER_BAD_LINK, .link = link_id);
  ERR_CHECK(s_dispatch.links[link_id] == NULL, ERR_ROUTER_BAD_LINK, .link = link_id);
  s_dispatch.links[link_id] = link;
  return NULL;
}

// A registered link, or the internal one for ROUTER_LINK_INTERNAL.
static const router_link_t* link_get(uint8_t link_id) {
  if (link_id == ROUTER_LINK_INTERNAL) {
    return &k_internal_link;
  }
  return link_id <= CONFIG_ROUTER_MAX_LINKS ? s_dispatch.links[link_id] : NULL;
}

// Response frame size the link carries right now, at most `buf_size`.
static uint32_t link_cap(const router_link_t* link, uint32_t buf_size) {
  uint32_t cap = link->max_frame != NULL ? link->max_frame() : CONFIG_ROUTER_FRAME_MAX;
  cap = cap < buf_size ? cap : buf_size;
  return cap > RESP_HDR_BYTES ? cap : RESP_HDR_BYTES;
}

/* ----------------------------------------------------------------------------
 * Answers
 * ------------------------------------------------------------------------- */

// The command a frame of `len` bytes describes; short frames give what they have.
static err_cmd_t frame_cmd(uint8_t link_id, const uint8_t* frame, uint32_t len) {
  return (err_cmd_t){
      .req = len > REQ_HDR_BYTES ? frame + REQ_HDR_BYTES : NULL,
      .req_len = len > REQ_HDR_BYTES ? len - REQ_HDR_BYTES : 0,
      .link = link_id,
      .seq = len > 0 ? frame[0] : 0,
      .flags = len > 1 ? frame[1] : 0,
  };
}

/* Ends a command's chain: logged with the command ("cmd uart seq 7 flags 00
 * req 02 02 00") and handed to the error sink with the command as origin. */
static void report_cmd_error(const router_link_t* link, const err_cmd_t* cmd, err_h err) {
  char ctx[48 + 3 * CONFIG_ERRORS_RECORD_REQ_MAX + 4];
  int pos = snprintf(ctx, sizeof(ctx), "cmd %s seq %u flags %02x req", link->name != NULL ? link->name : "?",
                     cmd->seq, cmd->flags);
  uint32_t shown = cmd->req_len < CONFIG_ERRORS_RECORD_REQ_MAX ? cmd->req_len : CONFIG_ERRORS_RECORD_REQ_MAX;
  for (uint32_t i = 0; i < shown && pos > 0 && (size_t)pos < sizeof(ctx); i++) {
    pos += snprintf(ctx + pos, sizeof(ctx) - pos, " %02x", cmd->req[i]);
  }
  if (shown < cmd->req_len && pos > 0 && (size_t)pos < sizeof(ctx)) {
    snprintf(ctx + pos, sizeof(ctx) - pos, " ...");
  }
  err_report_ex(err, ctx, cmd);
}

/* Builds [stream][seq][status][data] in `resp` (data already after the header
 * when OK; the error record when `err`), sends it and reports `err`. */
static void answer(const router_link_t* link, const err_cmd_t* cmd, err_h err, uint8_t* resp, uint32_t cap,
                   uint32_t data_len) {
  resp[0] = CONFIG_ROUTER_RESPONSE_STREAM;
  resp[1] = cmd->seq;
  resp[2] = err != NULL ? ROUTER_STATUS_ERROR : ROUTER_STATUS_OK;
  if (err != NULL) {
    data_len = err_record(cmd, err, resp + RESP_HDR_BYTES, cap - RESP_HDR_BYTES);
  }
  if (link->send != NULL) {
    err_report(link->send(resp, RESP_HDR_BYTES + data_len));
  }
  if (err != NULL) {
    report_cmd_error(link, cmd, err);
  }
}

/* ----------------------------------------------------------------------------
 * Queue: router_submit and router_inject
 * ------------------------------------------------------------------------- */

static void notify_urgent(uint32_t bits) {
  if (router_urgent_task_h != NULL) {
    xTaskNotify(router_urgent_task_h, bits, eSetBits);
  }
}

/* Copies [link][head][body] into the urgent or work ring and wakes the urgent
 * task. false when the ring is full. */
static bool enqueue(bool urgent, uint8_t link_id, const uint8_t* head, uint32_t head_len, const uint8_t* body,
                    uint32_t body_len) {
  RingbufHandle_t rb = urgent ? router_urgent_rb : router_work_rb;
  void* slot = NULL;
  if (xRingbufferSendAcquire(rb, &slot, 1 + head_len + body_len, 0) != pdTRUE) {
    return false;
  }
  ((uint8_t*)slot)[0] = link_id;
  memcpy((uint8_t*)slot + 1, head, head_len);
  if (body_len > 0) {
    memcpy((uint8_t*)slot + 1 + head_len, body, body_len);
  }
  xRingbufferSendComplete(rb, slot);
  if (urgent) {
    notify_urgent(NOTIFY_FRAME);
  }
  return true;
}

/* A frame that was not queued: answered when it has a seq to answer to, and
 * reported with its command. */
static void reject(const router_link_t* link, uint8_t link_id, const uint8_t* frame, uint32_t len, err_h err) {
  const err_cmd_t cmd = frame_cmd(link_id, frame, len);
  if (len == 0) {
    report_cmd_error(link, &cmd, err);
    return;
  }
  uint8_t resp[REJECT_RESP_BYTES];
  answer(link, &cmd, err, resp, link_cap(link, sizeof(resp)), 0);
}

err_h router_submit(uint8_t link_id, const uint8_t* frame, uint32_t len) {
  const router_link_t* link = link_get(link_id);
  ERR_CHECK(link != NULL && link_id != ROUTER_LINK_INTERNAL, ERR_ROUTER_BAD_LINK, .link = link_id);
  ERR_CHECK(frame != NULL || len == 0, ERR_CMD_BAD_ARG);

  if (len < REQ_MIN_BYTES || len > CONFIG_ROUTER_FRAME_MAX) {
    reject(link, link_id, frame, len,
           ERR_NEW(ERR_ROUTER_FRAME_LEN, .link = link_id, .len = (uint16_t)len, .max = CONFIG_ROUTER_FRAME_MAX));
    return NULL;
  }
  const router_watch_t* w = s_dispatch.watch;
  if (w != NULL && w->frame != NULL) {
    w->frame();
  }

  bool urgent = (frame[1] & ROUTER_FLAG_URGENT) != 0;
  if (!enqueue(urgent, link_id, frame, len, NULL, 0)) {
    reject(link, link_id, frame, len, ERR_NEW(ERR_ROUTER_QUEUE_FULL, .link = link_id, .urgent = urgent));
  }
  return NULL;
}

err_h router_inject(const uint8_t* req, uint32_t len, bool urgent) {
  ERR_CHECK(req != NULL && len > 0, ERR_CMD_BAD_ARG);
  ERR_CHECK(len <= CONFIG_ROUTER_FRAME_MAX - REQ_HDR_BYTES, ERR_ROUTER_FRAME_LEN, .link = ROUTER_LINK_INTERNAL,
            .len = (uint16_t)(len + REQ_HDR_BYTES), .max = CONFIG_ROUTER_FRAME_MAX);
  const uint8_t head[REQ_HDR_BYTES] = {__atomic_fetch_add(&s_dispatch.inject_seq, 1, __ATOMIC_RELAXED),
                                       urgent ? ROUTER_FLAG_URGENT : 0};
  ERR_CHECK(enqueue(urgent, ROUTER_LINK_INTERNAL, head, REQ_HDR_BYTES, req, len), ERR_ROUTER_QUEUE_FULL,
            .link = ROUTER_LINK_INTERNAL, .urgent = urgent);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Tap and origin
 * ------------------------------------------------------------------------- */

void router_set_tap(const router_tap_t* tap) {
  s_dispatch.tap = tap;
}

const err_cmd_t* router_origin(void) {
  return s_route != NULL ? s_route->cmd : NULL;
}

err_h router_answer_part(const uint8_t* out, uint32_t len) {
  const route_t* r = s_route;
  ERR_CHECK(r != NULL, ERR_CMD_BAD_ARG);
  if (out != r->resp + RESP_HDR_BYTES) {
    return NULL;  // a replay (an action's nested route): its answers are dropped
  }
  answer(r->link, r->cmd, NULL, r->resp, 0, len);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Watch
 * ------------------------------------------------------------------------- */

void router_set_watch(const router_watch_t* watch) {
  s_dispatch.watch = watch;
}

void router_wake_urgent(void) {
  notify_urgent(NOTIFY_WATCH);
}

/* ----------------------------------------------------------------------------
 * Tasks
 * ------------------------------------------------------------------------- */

// Routes one queued item [link][frame] and answers it. Frame length checked by router_submit.
static void process(const uint8_t* item, size_t size, uint8_t* resp) {
  const uint8_t link_id = item[0];
  const uint8_t* frame = item + 1;
  uint32_t len = (uint32_t)(size - 1);
  const router_link_t* link = link_get(link_id);  // links are never removed

  const err_cmd_t cmd = frame_cmd(link_id, frame, len);
  uint32_t cap = link_cap(link, CONFIG_ROUTER_FRAME_MAX);
  uint32_t data_len = 0;
  err_h err = NULL;
  const router_tap_t* tap = s_dispatch.tap;
  if (tap == NULL || tap->take == NULL || !tap->take(&cmd)) {
    const route_t route = {.cmd = &cmd, .link = link, .resp = resp};
    s_route = &route;
    err = router_route(cmd.req, cmd.req_len, resp + RESP_HDR_BYTES, cap - RESP_HDR_BYTES, &data_len);
    s_route = NULL;
    if (tap != NULL && tap->seen != NULL) {
      tap->seen(&cmd, err);
    }
  }
  answer(link, &cmd, err, resp, cap, data_len);
}

static void work_task(void* arg) {
  (void)arg;
  while (1) {
    size_t size = 0;
    uint8_t* item = xRingbufferReceive(router_work_rb, &size, WAIT_FOREVER);
    if (item != NULL) {
      process(item, size, s_dispatch.resp_work);
      vRingbufferReturnItem(router_work_rb, item);
    }
  }
}

static void urgent_task(void* arg) {
  (void)arg;
  while (1) {
    // Drain first: frames may have been queued before the task started.
    size_t size = 0;
    uint8_t* item;
    while ((item = xRingbufferReceive(router_urgent_rb, &size, NO_WAIT)) != NULL) {
      process(item, size, s_dispatch.resp_urgent);
      vRingbufferReturnItem(router_urgent_rb, item);
    }

    uint32_t bits = 0;
    U_NOTIFY_AWAIT(WAIT_FOREVER, &bits);
    const router_watch_t* w = s_dispatch.watch;
    if ((bits & NOTIFY_WATCH) && w != NULL && w->urgent != NULL) {
      w->urgent();
    }
  }
}

err_h router_init(void) {
  U_TASK_START(router_urgent_task_h, urgent_task, NULL, CONFIG_ROUTER_URGENT_TASK_PRIO);
  U_TASK_START(router_work_task_h, work_task, NULL, CONFIG_ROUTER_WORK_TASK_PRIO);
  return NULL;
}
