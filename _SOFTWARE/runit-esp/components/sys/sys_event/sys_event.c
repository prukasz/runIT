#include "sys_event.h"
#include "router_ids.h"
#include "sdkconfig.h"
#include "sys_connector.h"
#include "utils.h"
#include "board_time.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_EVENT, "event", ERR_SYS_EVENT_MAP)

_Static_assert(CONFIG_SYS_EVENT_MAX_SUBS <= 255, "subscription handles are u8");

/* A subscription: a device listener (device set, inline) or a function listener (fn set, the event task). */
typedef struct {
  bool used;
  uint8_t channel;
  uint16_t source;
  uint16_t event;
  uint16_t device;  // SYS_DEVICE_ID_NONE: a function listener
  sys_event_handler_f fn;
  void* ctx;
} sub_t;

/* What the event task still has to do with a queued event. */
typedef enum {
  STAGE_QUEUED,    // published inline: only the function listeners are left
  STAGE_DEFERRED,  // nothing done yet: notify, the device listeners, the function listeners
  STAGE_FROM_ISR,  // as DEFERRED, and t_ms is still the uptime (the clock can't be read in an ISR)
} stage_e;

typedef struct {
  sys_event_t ev;
  uint8_t stage;  // stage_e
} queued_t;

U_QUEUE_DEFINE(sys_event_queue, CONFIG_SYS_EVENT_QUEUE_LEN, sizeof(queued_t));
U_TASK_DEFINE(sys_event_task_h, CONFIG_SYS_EVENT_TASK_STACK);

static struct {
  sub_t subs[CONFIG_SYS_EVENT_MAX_SUBS];  // under s_lock
  /* ISR events the queue couldn't take. Counts up and wraps; only the ISR side writes it, the task reads it (an
     aligned u32 is read and written whole) and reports what it has not reported yet. */
  volatile uint32_t isr_drops;
  uint32_t isr_drops_reported;  // event task only
} s_event;
static U_SPINLOCK_DEFINE(s_lock);  // s_event.subs: short copies only, never held while calling out

/* ----------------------------------------------------------------------------
 * Matching and delivery
 * ------------------------------------------------------------------------- */

bool sys_event_matches(uint16_t source, uint16_t event, uint8_t channel, const sys_event_t* e) {
  if (source != SYS_EVENT_ANY_SOURCE && source != e->source) {
    return false;
  }
  if (channel != SYS_EVENT_ANY_CHANNEL && channel != e->channel) {
    return false;
  }
  if (event == SYS_EVENT_ANY_EVENT) {
    return true;
  }
  if ((event & 0xFFu) == SYS_EVENT_ANY_IN) {
    return (event >> 8) == (e->event >> 8);
  }
  return event == e->event;
}

// A used slot of the kind asked for (device listeners or function listeners) that matches the event.
static bool sub_matches(const sub_t* s, const sys_event_t* e, bool devices) {
  return s->used && (s->device != SYS_DEVICE_ID_NONE) == devices &&
         sys_event_matches(s->source, s->event, s->channel, e);
}

/* Copies subscription i if it matches. One slot per lock: a copy of the whole table would take
   CONFIG_SYS_EVENT_MAX_SUBS x 16 B of the publisher's stack (an esp_timer task, a router task). */
static bool take_sub(uint32_t i, const sys_event_t* e, bool devices, sub_t* out) {
  U_CRITICAL_ENTER(s_lock);
  const bool hit = sub_matches(&s_event.subs[i], e, devices);
  if (hit) {
    *out = s_event.subs[i];
  }
  U_CRITICAL_EXIT(s_lock);
  return hit;
}

// A listener's failure is reported with the event; the other listeners still run.
static void deliver(const sub_t* sub, const sys_event_t* e) {
  err_h err = sub->device != SYS_DEVICE_ID_NONE ? sys_device_write(sub->device, EVENT_OP_ON_EVENT, e, sizeof(*e))
                                                : sub->fn(e, sub->ctx);
  if (err != NULL) {
    err_report(ERR_WRAP(err, ERR_EVENT_DELIVERY, .source = e->source, .event = e->event, .channel = e->channel,
                        .device = sub->device));
  }
}

static void run_listeners(const sys_event_t* e, bool devices) {
  sub_t sub;
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBS; i++) {
    if (take_sub(i, e, devices, &sub)) {
      deliver(&sub, e);
    }
  }
}

static bool any_fn_listener(const sys_event_t* e) {
  bool any = false;
  U_CRITICAL_ENTER(s_lock);
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBS && !any; i++) {
    any = sub_matches(&s_event.subs[i], e, false);
  }
  U_CRITICAL_EXIT(s_lock);
  return any;
}

/* On the events connector, when NOTIFY: a frame in the command shape, [ROUTER_KEY_EVENT][SYS_EVENT_OP_NOTIFY] and the
 * event itself (sys_event_t, 18 B packed). A full connector queue drops it (like the errors stream: reporting that
 * would add traffic to the same queue). */
static void notify(const sys_event_t* e) {
  if (!(e->flags & SYS_EVENT_NOTIFY)) {
    return;
  }
  uint8_t frame[2 + sizeof(*e)] = {ROUTER_KEY_EVENT, SYS_EVENT_OP_NOTIFY};
  memcpy(frame + 2, e, sizeof(*e));
  err_free(sys_connector_send(SYS_CONNECTOR_EVENTS, frame, sizeof(frame)));
}

// What runs where the event is published, or in the task for a deferred one: notify first (whatever the listeners
// do, an error policy stopping the board, the cause is out), then the device listeners.
static void deliver_inline(const sys_event_t* e) {
  notify(e);
  run_listeners(e, true);
}

/* ----------------------------------------------------------------------------
 * Publish and the event task
 * ------------------------------------------------------------------------- */

static err_h enqueue(const sys_event_t* ev, stage_e stage) {
  const queued_t q = {.ev = *ev, .stage = stage};
  ERR_CHECK(xQueueSend(sys_event_queue, &q, NO_WAIT) == pdTRUE, ERR_EVENT_QUEUE_FULL, .source = ev->source,
            .event = ev->event, .dropped = 1);
  return NULL;
}

// ISR: the whole delivery goes to the task; a drop is counted, never an error (an ISR can't report).
static void publish_from_isr(const sys_event_t* event) {
  queued_t q = {.ev = *event, .stage = STAGE_FROM_ISR};
  q.ev.t_ms = board_time_uptime_ms();  // the clock takes a lock: the event task converts it
  BaseType_t woken = pdFALSE;
  if (xQueueSendFromISR(sys_event_queue, &q, &woken) != pdTRUE) {
    s_event.isr_drops++;
  }
  portYIELD_FROM_ISR(woken);
}

err_h sys_event_publish(const sys_event_t* event) {
  if (xPortInIsrContext()) {
    if (event != NULL) {
      publish_from_isr(event);
    }
    return NULL;
  }
  ERR_CHECK(event != NULL, ERR_EVENT_BAD_ARG);
  sys_event_t ev = *event;
  ev.t_ms = board_time_now_ms();
  deliver_inline(&ev);
  return any_fn_listener(&ev) ? enqueue(&ev, STAGE_QUEUED) : NULL;
}

err_h sys_event_publish_deferred(const sys_event_t* event) {
  ERR_CHECK(event != NULL, ERR_EVENT_BAD_ARG);
  sys_event_t ev = *event;
  ev.t_ms = board_time_now_ms();
  return enqueue(&ev, STAGE_DEFERRED);
}

static void event_task(void* arg) {
  (void)arg;
  queued_t q;
  while (1) {
    if (xQueueReceive(sys_event_queue, &q, WAIT_FOREVER) != pdTRUE) {
      continue;
    }
    const uint32_t dropped = s_event.isr_drops - s_event.isr_drops_reported;  // unsigned: right across the wrap
    if (dropped > 0) {
      s_event.isr_drops_reported += dropped;
      err_report(ERR_NEW(ERR_EVENT_QUEUE_FULL, .source = SYS_EVENT_ANY_SOURCE, .event = SYS_EVENT_ANY_EVENT,
                         .dropped = dropped));
    }
    if (q.stage == STAGE_FROM_ISR) {
      q.ev.t_ms = board_time_at_uptime_ms(q.ev.t_ms);
    }
    if (q.stage != STAGE_QUEUED) {
      deliver_inline(&q.ev);
    }
    run_listeners(&q.ev, false);
  }
}

err_h sys_event_init(void) {
  if (sys_event_task_h == NULL) {
    U_TASK_START(sys_event_task_h, event_task, NULL, CONFIG_SYS_EVENT_TASK_PRIO);
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Subscriptions
 * ------------------------------------------------------------------------- */

static err_h add(const sub_t* sub, uint8_t* handle) {
  ERR_CHECK(handle != NULL, ERR_EVENT_BAD_ARG);
  bool added = false;
  U_CRITICAL_ENTER(s_lock);
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBS && !added; i++) {
    if (!s_event.subs[i].used) {
      s_event.subs[i] = *sub;
      *handle = (uint8_t)i;
      added = true;
    }
  }
  U_CRITICAL_EXIT(s_lock);
  ERR_CHECK(added, ERR_EVENT_TABLE_FULL);
  return NULL;
}

err_h sys_event_subscribe_device(uint16_t source, uint16_t event, uint8_t channel, uint16_t device,
                                 uint8_t* handle) {
  ERR_CHECK(device != SYS_DEVICE_ID_NONE, ERR_EVENT_BAD_ARG);
  const sub_t sub = {.used = true, .source = source, .event = event, .channel = channel, .device = device};
  return add(&sub, handle);
}

err_h sys_event_subscribe_fn(uint16_t source, uint16_t event, uint8_t channel, sys_event_handler_f fn, void* ctx,
                             uint8_t* handle) {
  ERR_CHECK(fn != NULL, ERR_EVENT_BAD_ARG);
  const sub_t sub = {.used = true,
                     .source = source,
                     .event = event,
                     .channel = channel,
                     .device = SYS_DEVICE_ID_NONE,
                     .fn = fn,
                     .ctx = ctx};
  return add(&sub, handle);
}

err_h sys_event_unsubscribe(uint8_t handle) {
  bool found = false;
  U_CRITICAL_ENTER(s_lock);
  if (handle < CONFIG_SYS_EVENT_MAX_SUBS && s_event.subs[handle].used) {
    s_event.subs[handle].used = false;
    found = true;
  }
  U_CRITICAL_EXIT(s_lock);
  ERR_CHECK(found, ERR_EVENT_NOT_FOUND, .id = handle);
  return NULL;
}
