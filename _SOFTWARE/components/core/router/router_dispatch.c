

#include "esp_log.h"
#include "router_internal.h"
#include "sdkconfig.h"

#define TAG __FILE_NAME__

#define RESP_HDR 3u            // [stream][seq][status]
#define REQ_HDR 2u             // [seq][flags]
#define REQ_MIN (REQ_HDR + 1)  // ... and a root key
#define EARLY_RESP_MAX 64u     // error answers sent from router_submit (link task stack)

#define BIT_FRAME (1u << 0)
#define BIT_DEADMAN (1u << 1)
#define BIT_LINK_LOST (1u << 2)

_Static_assert(CONFIG_ROUTER_WORK_QUEUE_SIZE % 4 == 0, "ROUTER_WORK_QUEUE_SIZE must be a multiple of 4");
_Static_assert(CONFIG_ROUTER_URGENT_QUEUE_SIZE % 4 == 0, "ROUTER_URGENT_QUEUE_SIZE must be a multiple of 4");

// Queued in front of every frame in the ring buffers.
typedef struct {
  uint32_t peer;
  uint8_t link;
} item_hdr_t;

static void deadman_expired(TimerHandle_t t);

U_RINGBUFFER_DEFINE(router_work_rb, CONFIG_ROUTER_WORK_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_RINGBUFFER_DEFINE(router_urgent_rb, CONFIG_ROUTER_URGENT_QUEUE_SIZE, RINGBUF_TYPE_NOSPLIT);
U_TASK_DEFINE(router_work_task_h, CONFIG_ROUTER_WORK_TASK_STACK);
U_TASK_DEFINE(router_urgent_task_h, CONFIG_ROUTER_URGENT_TASK_STACK);
U_TIMER_DEFINE(router_deadman_timer, 1, false, deadman_expired);

static const router_link_t* s_links[CONFIG_ROUTER_MAX_LINKS + 1];  // index = link ID, 0 unused
static U_SPINLOCK_DEFINE(s_lock);                                   // s_links writes, dead-man config

static router_safe_state_f s_safe_state;
static uint32_t s_deadman_ms = CONFIG_ROUTER_DEADMAN_DEFAULT_MS;
static bool s_deadman_link_lost;

static const router_tap_t* s_tap;
static __thread const router_cmd_t* s_origin;  // per router task: the command being routed
static __thread const router_link_t* s_origin_link;
static __thread uint8_t* s_origin_resp;  // that task's response buffer (router_answer_part)
static uint8_t s_inject_seq;                   // seq of injected frames (diagnostics only)

// Link 0: frames from the board itself (router_inject). Answers go nowhere.
static err_h internal_send(uint32_t peer, const uint8_t* frame, uint32_t len) {
  (void)peer;
  (void)frame;
  (void)len;
  return NULL;
}

static const router_link_t k_internal_link = {.name = "internal", .send = internal_send};

// One response buffer per task: the two tasks build answers at the same time.
static uint8_t s_resp_work[CONFIG_ROUTER_FRAME_MAX];
static uint8_t s_resp_urgent[CONFIG_ROUTER_FRAME_MAX];

/* ----------------------------------------------------------------------------
 * Safe state and dead-man
 * ------------------------------------------------------------------------- */

// The safe state as noted for SAFE_GET: the reason of the last entry (0 = not in it) and its tick. Written by whoever enters it (the
// router's task, the dead-man timer, the power manager), read by SAFE_GET: single words, a torn pair only skews the age by one entry.
static volatile uint8_t s_safe_reason;
static volatile TickType_t s_safe_since;
static router_safe_leave_f s_safe_leave;

void router_set_safe_state(router_safe_state_f fn) {
  s_safe_state = fn;
}

void router_set_safe_leave(router_safe_leave_f fn) {
  s_safe_leave = fn;
}

void router_enter_safe_state(router_safe_reason_e reason) {
  ESP_LOGW(TAG, "safe state (reason %d)", (int)reason);
  s_safe_since = xTaskGetTickCount();
  s_safe_reason = (uint8_t)reason;
  if (s_safe_state != NULL) {
    s_safe_state(reason);
  }
}

void router_safe_state_get(uint8_t* reason, uint32_t* for_ms) {
  *reason = s_safe_reason;
  *for_ms = *reason == 0 ? 0u : (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount() - s_safe_since);
}

err_h router_leave_safe_state(void) {
  if (s_safe_leave != NULL) {
    ERR_TRY(s_safe_leave());
  }
  s_safe_reason = 0;
  return NULL;
}

static void notify_urgent(uint32_t bits) {
  if (router_urgent_task_h != NULL) {
    xTaskNotify(router_urgent_task_h, bits, eSetBits);
  }
}

// Timer task: hand the work to the urgent task (the handler may block on I/O).
static void deadman_expired(TimerHandle_t t) {
  (void)t;
  notify_urgent(BIT_DEADMAN);
}

void router_deadman_set(uint32_t timeout_ms, bool on_link_lost) {
  U_CRITICAL_ENTER(s_lock);
  s_deadman_ms = timeout_ms;
  s_deadman_link_lost = on_link_lost;
  U_CRITICAL_EXIT(s_lock);
  if (timeout_ms > 0) {
    xTimerChangePeriod(router_deadman_timer, MSEC(timeout_ms), 0);  // also (re)starts it
  } else {
    U_TIMER_STOP(router_deadman_timer);
  }
}

void router_deadman_get(uint32_t* timeout_ms, bool* on_link_lost) {
  U_CRITICAL_ENTER(s_lock);
  *timeout_ms = s_deadman_ms;
  *on_link_lost = s_deadman_link_lost;
  U_CRITICAL_EXIT(s_lock);
}

// Any frame from any link proves the client is alive; re-arms after a trigger.
static void deadman_feed(void) {
  if (s_deadman_ms > 0) {
    U_TIMER_RESET(router_deadman_timer);
  }
}

void router_link_lost(void) {
  if (s_deadman_ms > 0 && s_deadman_link_lost) {
    notify_urgent(BIT_LINK_LOST);
  }
}

/* ----------------------------------------------------------------------------
 * Links and responses
 * ------------------------------------------------------------------------- */

err_h router_link_register(uint8_t link_id, const router_link_t* link) {
  ERR_CHECK(link != NULL && link->send != NULL, ERR_CMD_BAD_ARG);
  ERR_CHECK(link_id > 0 && link_id <= CONFIG_ROUTER_MAX_LINKS, ERR_ROUTER_BAD_LINK, .link = link_id);
  bool taken;
  U_CRITICAL_ENTER(s_lock);
  taken = s_links[link_id] != NULL;
  if (!taken) {
    s_links[link_id] = link;
  }
  U_CRITICAL_EXIT(s_lock);
  ERR_CHECK(!taken, ERR_ROUTER_BAD_LINK, .link = link_id);
  return NULL;
}

// A registered link, or the internal one for ROUTER_LINK_INTERNAL.
static const router_link_t* link_get(uint8_t link_id) {
  if (link_id == ROUTER_LINK_INTERNAL) {
    return &k_internal_link;
  }
  return link_id <= CONFIG_ROUTER_MAX_LINKS ? s_links[link_id] : NULL;
}

// Response frame size for this peer right now, at most `buf_size`.
static uint32_t link_cap(const router_link_t* link, uint32_t peer, uint32_t buf_size) {
  uint32_t cap = link->max_frame != NULL ? link->max_frame(peer) : CONFIG_ROUTER_FRAME_MAX;
  cap = cap < buf_size ? cap : buf_size;
  return cap > RESP_HDR ? cap : RESP_HDR;
}

/* Builds [stream][seq][status][data] in `resp` (data already at resp + RESP_HDR
 * when OK; the error record when `err`) and sends it. Does not free `err`. */
static void answer(const router_link_t* link, const router_cmd_t* cmd, err_h err, uint8_t* resp, uint32_t cap,
                   uint32_t data_len) {
  resp[0] = CONFIG_ROUTER_RESPONSE_STREAM;
  resp[1] = cmd->seq;
  if (err != NULL) {
    resp[2] = ROUTER_STATUS_ERROR;
    data_len = router_err_record(cmd, err, resp + RESP_HDR, cap - RESP_HDR);
  } else {
    resp[2] = ROUTER_STATUS_OK;
  }
  err_report(link->send(cmd->peer, resp, RESP_HDR + data_len));
}

/* Ends a command's chain: logged with the command ("cmd uart seq 7 flags 00
 * req 02 02 00") and handed to the error sink with the command as origin. */
static void report_cmd_error(const router_link_t* link, const router_cmd_t* cmd, err_h err) {
  char ctx[48 + 3 * CONFIG_ROUTER_ERR_REQ_MAX + 4];
  int pos = snprintf(ctx, sizeof(ctx), "cmd %s seq %u flags %02x req", link->name != NULL ? link->name : "?",
                     cmd->seq, cmd->flags);
  uint32_t shown = cmd->req_len < CONFIG_ROUTER_ERR_REQ_MAX ? cmd->req_len : CONFIG_ROUTER_ERR_REQ_MAX;
  for (uint32_t i = 0; i < shown && pos > 0 && (size_t)pos < sizeof(ctx); i++) {
    pos += snprintf(ctx + pos, sizeof(ctx) - pos, " %02x", cmd->req[i]);
  }
  if (shown < cmd->req_len && pos > 0 && (size_t)pos < sizeof(ctx)) {
    snprintf(ctx + pos, sizeof(ctx) - pos, " ...");
  }
  err_report_ex(err, ctx, cmd);
}

// The command a frame of `len` bytes describes; short frames give what they have.
static router_cmd_t frame_cmd(uint8_t link_id, uint32_t peer, const uint8_t* frame, uint32_t len) {
  return (router_cmd_t){
      .peer = peer,
      .req = len > REQ_HDR ? frame + REQ_HDR : NULL,
      .req_len = len > REQ_HDR ? len - REQ_HDR : 0,
      .link = link_id,
      .seq = len > 0 ? frame[0] : 0,
      .flags = len > 1 ? frame[1] : 0,
  };
}

/* ----------------------------------------------------------------------------
 * Submit and tasks
 * ------------------------------------------------------------------------- */

/* A frame that was not queued: answered when it has a seq to answer to, and
 * reported with its command. */
static void reject(const router_link_t* link, uint8_t link_id, uint32_t peer, const uint8_t* frame, uint32_t len,
                   err_h err) {
  const router_cmd_t cmd = frame_cmd(link_id, peer, frame, len);
  if (len > 0) {
    uint8_t resp[EARLY_RESP_MAX];
    answer(link, &cmd, err, resp, link_cap(link, peer, sizeof(resp)), 0);
  }
  report_cmd_error(link, &cmd, err);
}

/* Copies [hdr][head][body] into the urgent or work ring and wakes the urgent
 * task. false when the ring is full. */
static bool enqueue(bool urgent, item_hdr_t hdr, const uint8_t* head, uint32_t head_len, const uint8_t* body,
                    uint32_t body_len) {
  RingbufHandle_t rb = urgent ? router_urgent_rb : router_work_rb;
  void* slot = NULL;
  if (xRingbufferSendAcquire(rb, &slot, sizeof(hdr) + head_len + body_len, 0) != pdTRUE) {
    return false;
  }
  memcpy(slot, &hdr, sizeof(hdr));
  memcpy((uint8_t*)slot + sizeof(hdr), head, head_len);
  if (body_len > 0) {
    memcpy((uint8_t*)slot + sizeof(hdr) + head_len, body, body_len);
  }
  xRingbufferSendComplete(rb, slot);
  if (urgent) {
    notify_urgent(BIT_FRAME);
  }
  return true;
}

err_h router_submit(uint8_t link_id, uint32_t peer, const uint8_t* frame, uint32_t len) {
  const router_link_t* link = link_get(link_id);
  ERR_CHECK(link != NULL && link_id != ROUTER_LINK_INTERNAL, ERR_ROUTER_BAD_LINK, .link = link_id);
  ERR_CHECK(frame != NULL || len == 0, ERR_CMD_BAD_ARG);

  if (len < REQ_MIN || len > CONFIG_ROUTER_FRAME_MAX) {
    reject(link, link_id, peer, frame, len,
           ERR_NEW(ERR_ROUTER_FRAME_LEN, .link = link_id, .len = (uint16_t)len, .max = CONFIG_ROUTER_FRAME_MAX));
    return NULL;
  }
  deadman_feed();

  bool urgent = (frame[1] & ROUTER_FLAG_URGENT) != 0;
  if (!enqueue(urgent, (item_hdr_t){.peer = peer, .link = link_id}, frame, len, NULL, 0)) {
    reject(link, link_id, peer, frame, len, ERR_NEW(ERR_ROUTER_QUEUE_FULL, .link = link_id, .urgent = urgent));
  }
  return NULL;
}

err_h router_inject(const uint8_t* req, uint32_t len, bool urgent) {
  ERR_CHECK(req != NULL && len > 0, ERR_CMD_BAD_ARG);
  ERR_CHECK(len <= CONFIG_ROUTER_FRAME_MAX - REQ_HDR, ERR_ROUTER_FRAME_LEN, .link = ROUTER_LINK_INTERNAL,
            .len = (uint16_t)(len + REQ_HDR), .max = CONFIG_ROUTER_FRAME_MAX);
  const uint8_t head[REQ_HDR] = {__atomic_fetch_add(&s_inject_seq, 1, __ATOMIC_RELAXED),
                                 urgent ? ROUTER_FLAG_URGENT : 0};
  ERR_CHECK(enqueue(urgent, (item_hdr_t){.peer = 0, .link = ROUTER_LINK_INTERNAL}, head, REQ_HDR, req, len),
            ERR_ROUTER_QUEUE_FULL, .link = ROUTER_LINK_INTERNAL, .urgent = urgent);
  return NULL;
}

void router_set_tap(const router_tap_t* tap) {
  s_tap = tap;
}

const router_cmd_t* router_origin(void) {
  return s_origin;
}

err_h router_answer_part(const uint8_t* out, uint32_t len) {
  ERR_CHECK(s_origin != NULL, ERR_CMD_BAD_ARG);
  if (s_origin_resp == NULL || out != s_origin_resp + RESP_HDR) {
    return NULL;  // a replay (an action's nested route): its answers are dropped
  }
  answer(s_origin_link, s_origin, NULL, s_origin_resp, 0, len);
  return NULL;
}

// Routes one queued item and answers it. Item length checked by router_submit.
static void process(const uint8_t* item, size_t size, uint8_t* resp) {
  item_hdr_t hdr;
  memcpy(&hdr, item, sizeof(hdr));
  const uint8_t* frame = item + sizeof(hdr);
  uint32_t len = (uint32_t)(size - sizeof(hdr));
  const router_link_t* link = link_get(hdr.link);  // links are never removed

  const router_cmd_t cmd = frame_cmd(hdr.link, hdr.peer, frame, len);
  uint32_t cap = link_cap(link, hdr.peer, CONFIG_ROUTER_FRAME_MAX);
  uint32_t data_len = 0;
  err_h err = NULL;
  const router_tap_t* tap = s_tap;
  if (tap == NULL || tap->take == NULL || !tap->take(&cmd)) {
    s_origin = &cmd;
    s_origin_link = link;
    s_origin_resp = resp;
    err = router_route(cmd.req, cmd.req_len, resp + RESP_HDR, cap - RESP_HDR, &data_len);
    s_origin = NULL;
    s_origin_resp = NULL;
    if (tap != NULL && tap->seen != NULL) {
      tap->seen(&cmd, err);
    }
  }
  answer(link, &cmd, err, resp, cap, data_len);
  if (err != NULL) {
    report_cmd_error(link, &cmd, err);
  }
}

static void work_task(void* arg) {
  (void)arg;
  while (1) {
    size_t size = 0;
    uint8_t* item = xRingbufferReceive(router_work_rb, &size, WAIT_FOREVER);
    if (item != NULL) {
      process(item, size, s_resp_work);
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
      process(item, size, s_resp_urgent);
      vRingbufferReturnItem(router_urgent_rb, item);
    }

    uint32_t bits = 0;
    U_NOTIFY_AWAIT(WAIT_FOREVER, &bits);
    if (bits & BIT_DEADMAN) {
      router_enter_safe_state(ROUTER_SAFE_DEADMAN);
    }
    if (bits & BIT_LINK_LOST) {
      router_enter_safe_state(ROUTER_SAFE_LINK_LOST);
    }
  }
}

err_h router_init(void) {
  ERR_TRY(router_sys_register());
  if (router_urgent_task_h == NULL) {
    U_TASK_START(router_urgent_task_h, urgent_task, NULL, CONFIG_ROUTER_URGENT_TASK_PRIO);
  }
  if (router_work_task_h == NULL) {
    U_TASK_START(router_work_task_h, work_task, NULL, CONFIG_ROUTER_WORK_TASK_PRIO);
  }
  if (s_deadman_ms > 0) {
    router_deadman_set(s_deadman_ms, s_deadman_link_lost);
  }
  return NULL;
}
