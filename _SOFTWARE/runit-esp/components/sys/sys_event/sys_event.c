#include "sys_event.h"
#include "router_ids.h"
#include "sdkconfig.h"
#include "sys_connector.h"
#include "utils.h"
#include "board_time.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_EVENT, "event", ERR_SYS_EVENT_MAP)

_Static_assert(CONFIG_SYS_EVENT_MAX_SUBS <= 255, "subscription handles are u8");

typedef struct {
  bool used;
  sys_event_sub_t sub;
} sub_slot_t;

/* The queue holds events. One with SYS_EVENT_DEFER (set by the publisher, or here for an ISR) has had nothing done
   yet: the task does notify and the inline listeners too. One without it was published inline: only the queued
   listeners are left. The task clears the flag before delivering, so no listener or stream record sees it. */
U_QUEUE_DEFINE(sys_event_queue, CONFIG_SYS_EVENT_QUEUE_LEN, sizeof(sys_event_t));
U_TASK_DEFINE(sys_event_task_h, CONFIG_SYS_EVENT_TASK_STACK);

static struct {
  sub_slot_t subs[CONFIG_SYS_EVENT_MAX_SUBS];  // under s_lock
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

static bool sub_matches(const sub_slot_t* s, const sys_event_t* e, bool inline_call) {
  return s->used && s->sub.inline_call == inline_call && sys_event_matches(s->sub.source, s->sub.event, s->sub.channel, e);
}

/* Copies subscription i if it matches. One slot per lock: a copy of the whole table would take
   CONFIG_SYS_EVENT_MAX_SUBS x 16 B of the publisher's stack (an esp_timer task, a router task). */
static bool take_sub(uint32_t i, const sys_event_t* e, bool inline_call, sys_event_sub_t* out) {
  U_CRITICAL_ENTER(s_lock);
  const bool hit = sub_matches(&s_event.subs[i], e, inline_call);
  if (hit) {
    *out = s_event.subs[i].sub;
  }
  U_CRITICAL_EXIT(s_lock);
  return hit;
}

// A listener's failure is reported with the event; the other listeners still run.
static void deliver(const sys_event_sub_t* sub, const sys_event_t* e) {
  err_h err = sub->device != SYS_DEVICE_ID_NONE
                  ? sys_device_write(sub->device, EVENT_OP_ON_EVENT, e, sizeof(*e))
                  : sub->fn(e, sub->ctx);
  if (err != NULL) {
    err_report(ERR_WRAP(err, ERR_EVENT_DELIVERY, .source = e->source, .event = e->event, .channel = e->channel,
                        .device = sub->device));
  }
}

static void run_listeners(const sys_event_t* e, bool inline_call) {
  sys_event_sub_t sub;
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBS; i++) {
    if (take_sub(i, e, inline_call, &sub)) {
      deliver(&sub, e);
    }
  }
}

static bool any_queued(const sys_event_t* e) {
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
// do, an action stopping the board, the cause is out), then the inline listeners.
static void deliver_inline(const sys_event_t* e) {
  notify(e);
  run_listeners(e, true);
}

/* ----------------------------------------------------------------------------
 * Publish and the event task
 * ------------------------------------------------------------------------- */

// ISR: the whole delivery goes to the task; a drop is counted, never an error (an ISR can't report).
static void publish_from_isr(const sys_event_t* event) {
  sys_event_t ev = *event;
  ev.flags |= SYS_EVENT_DEFER | SYS_EVENT_FROM_ISR;
  ev.t_ms = board_time_uptime_ms();  // the clock takes a lock: the event task converts it
  BaseType_t woken = pdFALSE;
  if (xQueueSendFromISR(sys_event_queue, &ev, &woken) != pdTRUE) {
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
  // DEFER: the whole delivery in the event task. Else notify and the inline listeners now, then one queued copy.
  if (!(ev.flags & SYS_EVENT_DEFER)) {
    deliver_inline(&ev);
    if (!any_queued(&ev)) {
      return NULL;
    }
  }
  ERR_CHECK(xQueueSend(sys_event_queue, &ev, NO_WAIT) == pdTRUE, ERR_EVENT_QUEUE_FULL, .source = ev.source,
            .event = ev.event, .dropped = 1);
  return NULL;
}

static void event_task(void* arg) {
  (void)arg;
  sys_event_t ev;
  while (1) {
    if (xQueueReceive(sys_event_queue, &ev, WAIT_FOREVER) != pdTRUE) {
      continue;
    }
    const uint32_t dropped = s_event.isr_drops - s_event.isr_drops_reported;  // unsigned: right across the wrap
    if (dropped > 0) {
      s_event.isr_drops_reported += dropped;
      err_report(ERR_NEW(ERR_EVENT_QUEUE_FULL, .source = SYS_EVENT_ANY_SOURCE, .event = SYS_EVENT_ANY_EVENT,
                         .dropped = dropped));
    }
    if (ev.flags & SYS_EVENT_FROM_ISR) {
      ev.flags &= (uint8_t)~SYS_EVENT_FROM_ISR;
      ev.t_ms = board_time_at_uptime_ms(ev.t_ms);
    }
    if (ev.flags & SYS_EVENT_DEFER) {
      ev.flags &= (uint8_t)~SYS_EVENT_DEFER;
      deliver_inline(&ev);
    }
    run_listeners(&ev, false);
  }
}

err_h sys_event_init(void) {
  if (sys_event_task_h == NULL) {
    U_TASK_START(sys_event_task_h, event_task, NULL, CONFIG_SYS_EVENT_TASK_PRIO);
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Subscriptions (C)
 * ------------------------------------------------------------------------- */

err_h sys_event_subscribe(const sys_event_sub_t* sub, uint8_t* handle) {
  ERR_CHECK(sub != NULL && handle != NULL && (sub->device != SYS_DEVICE_ID_NONE || sub->fn != NULL),
            ERR_EVENT_BAD_ARG);
  bool added = false;
  U_CRITICAL_ENTER(s_lock);
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBS && !added; i++) {
    if (!s_event.subs[i].used) {
      s_event.subs[i] = (sub_slot_t){.used = true, .sub = *sub};
      *handle = (uint8_t)i;
      added = true;
    }
  }
  U_CRITICAL_EXIT(s_lock);
  ERR_CHECK(added, ERR_EVENT_TABLE_FULL);
  return NULL;
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
