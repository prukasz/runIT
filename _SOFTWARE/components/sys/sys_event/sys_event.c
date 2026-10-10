#include "sys_event.h"
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include "sys_connector.h"
#include "utils.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_EVENT, "event", ERR_SYS_EVENT_MAP)

#define RULE_ID_MIN 1u
#define RULE_ID_MAX 0xFEu  // 0xFF = SYS_EVENT_RULE_ALL

_Static_assert(CONFIG_SYS_EVENT_MAX_SUBS <= 255, "subscription handles are u8");

typedef struct {
  bool used;
  sys_event_sub_t sub;
} sub_slot_t;

typedef struct {
  bool used;
  sys_event_rule_t rule;
} rule_slot_t;

typedef struct {
  sys_event_t ev;
  bool deferred;  // ISR or sys_event_post: nothing done yet, notify and inline listeners run in the task
} item_t;

/* What one delivery needs, copied out of the table under the lock. */
typedef struct {
  bool is_rule;
  sys_event_sub_t sub;
  sys_event_rule_t rule;
} target_t;

/* On the events connector: [source u16][event u16][channel u8][value i32][t_ms u32]. */
typedef struct __attribute__((packed)) {
  uint16_t source;
  uint16_t event;
  uint8_t channel;
  int32_t value;
  uint32_t t_ms;
} notify_rec_t;

U_QUEUE_DEFINE(sys_event_queue, CONFIG_SYS_EVENT_QUEUE_LEN, sizeof(item_t));
U_TASK_DEFINE(sys_event_task_h, CONFIG_SYS_EVENT_TASK_STACK);

static sub_slot_t s_subs[CONFIG_SYS_EVENT_MAX_SUBS];
static rule_slot_t s_rules[CONFIG_SYS_EVENT_MAX_RULES];
static U_SPINLOCK_DEFINE(s_lock);  // s_subs, s_rules: short copies only, never held while calling out
static sys_event_action_f s_runner;
static uint32_t s_isr_drops;  // atomic: ISR events the queue couldn't take

/* ----------------------------------------------------------------------------
 * Matching and delivery
 * ------------------------------------------------------------------------- */

static bool matches(uint16_t source, uint16_t event, uint8_t channel, const sys_event_t* e) {
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

// Copies subscription i if it is used, has this delivery kind and matches.
static bool take_sub(uint32_t i, const sys_event_t* e, bool inline_call, target_t* t) {
  U_CRITICAL_ENTER(s_lock);
  const sub_slot_t* s = &s_subs[i];
  bool hit = s->used && s->sub.inline_call == inline_call && matches(s->sub.source, s->sub.event, s->sub.channel, e);
  if (hit) {
    t->is_rule = false;
    t->sub = s->sub;
  }
  U_CRITICAL_EXIT(s_lock);
  return hit;
}

// Copies rule i if it is used and matches (rules are always queued).
static bool take_rule(uint32_t i, const sys_event_t* e, target_t* t) {
  U_CRITICAL_ENTER(s_lock);
  const rule_slot_t* r = &s_rules[i];
  bool hit = r->used && matches(r->rule.source, r->rule.event, r->rule.channel, e);
  if (hit) {
    t->is_rule = true;
    t->rule = r->rule;
  }
  U_CRITICAL_EXIT(s_lock);
  return hit;
}

static err_h call_target(const target_t* t, const sys_event_t* e) {
  if (t->is_rule) {
    sys_event_action_f run = s_runner;
    ERR_CHECK(run != NULL, ERR_EVENT_NO_RUNNER, .action = t->rule.action);
    return run(t->rule.action, (t->rule.flags & SYS_EVENT_RULE_URGENT) != 0);
  }
  if (t->sub.device != SYS_DEVICE_ID_NONE) {
    return sys_device_call(t->sub.device, EVENT_OP_ON_EVENT, e, sizeof(*e), NULL, 0, NULL);
  }
  return t->sub.fn(e, t->sub.ctx);
}

// A listener's failure is reported with the event; the other listeners still run.
static void deliver(const target_t* t, const sys_event_t* e) {
  err_h err = call_target(t, e);
  if (err != NULL) {
    err_report(ERR_WRAP(err, ERR_EVENT_DELIVERY, .source = e->source, .event = e->event, .channel = e->channel,
                        .rule = t->is_rule ? t->rule.id : 0,
                        .device = t->is_rule ? SYS_DEVICE_ID_NONE : t->sub.device));
  }
}

static void run_listeners(const sys_event_t* e, bool inline_call) {
  target_t t;
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBS; i++) {
    if (take_sub(i, e, inline_call, &t)) {
      deliver(&t, e);
    }
  }
  if (inline_call) {
    return;
  }
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_RULES; i++) {
    if (take_rule(i, e, &t)) {
      deliver(&t, e);
    }
  }
}

static bool any_queued(const sys_event_t* e) {
  bool any = false;
  U_CRITICAL_ENTER(s_lock);
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBS && !any; i++) {
    const sub_slot_t* s = &s_subs[i];
    any = s->used && !s->sub.inline_call && matches(s->sub.source, s->sub.event, s->sub.channel, e);
  }
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_RULES && !any; i++) {
    const rule_slot_t* r = &s_rules[i];
    any = r->used && matches(r->rule.source, r->rule.event, r->rule.channel, e);
  }
  U_CRITICAL_EXIT(s_lock);
  return any;
}

/* On the events connector. A full connector queue drops it (like the errors
 * stream: reporting that would add traffic to the same queue). */
static void notify(const sys_event_t* e) {
  const notify_rec_t rec = {
      .source = e->source, .event = e->event, .channel = e->channel, .value = e->value, .t_ms = e->t_ms};
  err_free(sys_connector_send(SYS_CONNECTOR_EVENTS, &rec, sizeof(rec)));
}

/* ----------------------------------------------------------------------------
 * Publish and the event task
 * ------------------------------------------------------------------------- */

static uint32_t now_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

// ISR: the whole delivery goes to the task; a drop is counted, never an error (an ISR can't report).
static void publish_from_isr(const sys_event_t* event) {
  item_t it = {.ev = *event, .deferred = true};
  it.ev.t_ms = now_ms();
  BaseType_t woken = pdFALSE;
  if (xQueueSendFromISR(sys_event_queue, &it, &woken) != pdTRUE) {
    __atomic_fetch_add(&s_isr_drops, 1, __ATOMIC_RELAXED);
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
  ERR_CHECK(event->hops < CONFIG_SYS_EVENT_MAX_HOPS, ERR_EVENT_LOOP, .source = event->source, .event = event->event,
            .hops = event->hops);

  item_t it = {.ev = *event, .deferred = false};
  it.ev.t_ms = now_ms();
  if (it.ev.flags & SYS_EVENT_NOTIFY) {
    notify(&it.ev);  // first: whatever the listeners do (an action stopping the board), the cause is out
  }
  run_listeners(&it.ev, true);
  if (any_queued(&it.ev)) {
    ERR_CHECK(xQueueSend(sys_event_queue, &it, NO_WAIT) == pdTRUE, ERR_EVENT_QUEUE_FULL, .source = it.ev.source,
              .event = it.ev.event, .dropped = 1);
  }
  return NULL;
}

err_h sys_event_post(const sys_event_t* event) {
  ERR_CHECK(event != NULL, ERR_EVENT_BAD_ARG);
  ERR_CHECK(event->hops < CONFIG_SYS_EVENT_MAX_HOPS, ERR_EVENT_LOOP, .source = event->source, .event = event->event,
            .hops = event->hops);
  item_t it = {.ev = *event, .deferred = true};
  it.ev.t_ms = now_ms();
  ERR_CHECK(xQueueSend(sys_event_queue, &it, NO_WAIT) == pdTRUE, ERR_EVENT_QUEUE_FULL, .source = it.ev.source,
            .event = it.ev.event, .dropped = 1);
  return NULL;
}

static void event_task(void* arg) {
  (void)arg;
  item_t it;
  while (1) {
    if (xQueueReceive(sys_event_queue, &it, WAIT_FOREVER) != pdTRUE) {
      continue;
    }
    uint32_t dropped = __atomic_exchange_n(&s_isr_drops, 0, __ATOMIC_RELAXED);
    if (dropped > 0) {
      err_report(ERR_NEW(ERR_EVENT_QUEUE_FULL, .source = SYS_EVENT_ANY_SOURCE, .event = SYS_EVENT_ANY_EVENT,
                         .dropped = dropped));
    }
    if (it.deferred) {
      if (it.ev.flags & SYS_EVENT_NOTIFY) {
        notify(&it.ev);
      }
      run_listeners(&it.ev, true);
    }
    run_listeners(&it.ev, false);
  }
}

err_h sys_event_init(void) {
  if (sys_event_task_h == NULL) {
    U_TASK_START(sys_event_task_h, event_task, NULL, CONFIG_SYS_EVENT_TASK_PRIO);
  }
  return NULL;
}

void sys_event_set_action_runner(sys_event_action_f fn) {
  s_runner = fn;
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
    if (!s_subs[i].used) {
      s_subs[i] = (sub_slot_t){.used = true, .sub = *sub};
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
  if (handle < CONFIG_SYS_EVENT_MAX_SUBS && s_subs[handle].used) {
    s_subs[handle].used = false;
    found = true;
  }
  U_CRITICAL_EXIT(s_lock);
  ERR_CHECK(found, ERR_EVENT_NOT_FOUND, .id = handle);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Rules
 * ------------------------------------------------------------------------- */

err_h sys_event_rule_set(const sys_event_rule_t* rule) {
  ERR_CHECK(rule != NULL, ERR_EVENT_BAD_ARG);
  ERR_CHECK(rule->id >= RULE_ID_MIN && rule->id <= RULE_ID_MAX, ERR_EVENT_BAD_ID, .id = rule->id);
  int32_t slot = -1;
  U_CRITICAL_ENTER(s_lock);
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_RULES; i++) {
    if (s_rules[i].used && s_rules[i].rule.id == rule->id) {
      slot = (int32_t)i;  // same ID: replaced
      break;
    }
    if (!s_rules[i].used && slot < 0) {
      slot = (int32_t)i;
    }
  }
  if (slot >= 0) {
    s_rules[slot] = (rule_slot_t){.used = true, .rule = *rule};
  }
  U_CRITICAL_EXIT(s_lock);
  ERR_CHECK(slot >= 0, ERR_EVENT_TABLE_FULL);
  return NULL;
}

err_h sys_event_rule_remove(uint8_t id) {
  ERR_CHECK(id == SYS_EVENT_RULE_ALL || (id >= RULE_ID_MIN && id <= RULE_ID_MAX), ERR_EVENT_BAD_ID, .id = id);
  bool found = false;
  U_CRITICAL_ENTER(s_lock);
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_RULES; i++) {
    if (s_rules[i].used && (id == SYS_EVENT_RULE_ALL || s_rules[i].rule.id == id)) {
      s_rules[i].used = false;
      found = true;
    }
  }
  U_CRITICAL_EXIT(s_lock);
  ERR_CHECK(found || id == SYS_EVENT_RULE_ALL, ERR_EVENT_NOT_FOUND, .id = id);
  return NULL;
}

// Copies rule `id` out of the table.
static bool rule_get(uint8_t id, sys_event_rule_t* out, uint8_t* total) {
  bool found = false;
  uint8_t n = 0;
  U_CRITICAL_ENTER(s_lock);
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_RULES; i++) {
    if (!s_rules[i].used) {
      continue;
    }
    n++;
    if (s_rules[i].rule.id == id) {
      *out = s_rules[i].rule;
      found = true;
    }
  }
  U_CRITICAL_EXIT(s_lock);
  if (total != NULL) {
    *total = n;
  }
  return found;
}

err_h sys_event_rule_list(uint8_t start, bool (*fn)(const sys_event_rule_t* rule, void* ctx), void* ctx,
                          uint8_t* total) {
  ERR_CHECK(fn != NULL, ERR_EVENT_BAD_ARG);
  sys_event_rule_t r;
  if (total != NULL) {
    (void)rule_get(0, &r, total);  // ID 0 is never used: only counts
  }
  for (uint32_t id = start > RULE_ID_MIN ? start : RULE_ID_MIN; id <= RULE_ID_MAX; id++) {
    if (rule_get((uint8_t)id, &r, NULL) && !fn(&r, ctx)) {
      break;
    }
  }
  return NULL;
}
