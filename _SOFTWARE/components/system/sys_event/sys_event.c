#include "sys_event.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include "utils.h"

#define TAG "SYS_EVENT"
#define OWNER OWNER_SYS_EVENT_BASE

_Static_assert(CONFIG_SYS_EVENT_MAX_SUBSCRIPTIONS <= 32, "The subscription table is tracked in a 32-bit mask");
_Static_assert(CONFIG_SYS_EVENT_ROUTE_COUNT <= 8, "Route masks are 8 bits");
_Static_assert(CONFIG_SYS_EVENT_MAX_SUBSCRIPTIONS < SYS_EVENT_SUB_ALL_USER, "Subscription IDs must stay below SYS_EVENT_SUB_ALL_USER");

/* Queue item: published from an ISR (inline listeners not run yet) or only
   waiting for the queued listeners. */
typedef struct {
  sys_event_t event;
  bool from_isr;
} event_item_t;

R_QUEUE_DEFINE(s_event_queue, CONFIG_SYS_EVENT_QUEUE_LEN, sizeof(event_item_t));
R_TASK_DEFINE(s_event_task, CONFIG_SYS_EVENT_TASK_STACK_SIZE);

/* Subscription table. Entries are read one at a time under s_lock and copied
   out, so a listener can (un)subscribe while being called. */
static sys_event_subscription_t s_table[CONFIG_SYS_EVENT_MAX_SUBSCRIPTIONS];
static uint32_t s_used;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static sys_event_route_f s_routes[CONFIG_SYS_EVENT_ROUTE_COUNT];
static sys_event_action_executor_f s_action_executor;
static uint32_t s_isr_dropped;

void sys_event_register_action_executor(sys_event_action_executor_f executor) {
  s_action_executor = executor;
}

static bool field_matches(uint8_t want, uint8_t got) {
  return want == SYS_EVENT_ANY || want == got;
}

static bool sub_matches(const sys_event_subscription_t* sub, const sys_event_t* ev) {
  return field_matches(sub->domain, ev->domain) && field_matches(sub->device_id, ev->device_id) && field_matches(sub->channel, ev->channel) &&
         field_matches(sub->event, ev->event);
}

/* Copy slot i into *out if it is used, has this delivery kind and matches. */
static bool take_match(uint8_t i, const sys_event_t* ev, bool inline_call, sys_event_subscription_t* out) {
  bool hit = false;
  taskENTER_CRITICAL(&s_lock);
  if ((s_used & (1u << i)) && s_table[i].inline_call == inline_call && sub_matches(&s_table[i], ev)) {
    *out = s_table[i];
    hit = true;
  }
  taskEXIT_CRITICAL(&s_lock);
  return hit;
}

static bool has_queued_listener(const sys_event_t* ev) {
  sys_event_subscription_t sub;
  for (uint8_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBSCRIPTIONS; i++) {
    if (take_match(i, ev, false, &sub)) return true;
  }
  return false;
}

#undef OWNER
#define OWNER OWNER_SYS_EVENT_DISPATCH

static void deliver_inline(const sys_event_t* ev) {
  sys_event_subscription_t sub;
  for (uint8_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBSCRIPTIONS; i++) {
    if (take_match(i, ev, true, &sub)) SE_REPORT(sub.handler(ev, sub.ctx));
  }
}

static void run_action(uint8_t scope, uint8_t id) {
  if (id == 0) return;
  if (s_action_executor == NULL) {
    SE_RAISE(ERR_EVENT_NO_EXECUTOR, id);
    return;
  }
  SE_REPORT(s_action_executor(scope, id));
}

/* Event task: every queued listener, in table order. Per listener: handler,
   routes, user action, then system action (even if the user action fails). */
static void deliver_queued(const sys_event_t* ev) {
  sys_event_subscription_t sub;
  for (uint8_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBSCRIPTIONS; i++) {
    if (!take_match(i, ev, false, &sub)) continue;
    if (sub.handler) SE_REPORT(sub.handler(ev, sub.ctx));
    for (uint8_t r = 0; r < CONFIG_SYS_EVENT_ROUTE_COUNT; r++) {
      if ((sub.route_mask & SYS_EVENT_ROUTE_BIT(r)) && s_routes[r]) s_routes[r](ev);
    }
    run_action(0x01, sub.dynamic_action_id);
    run_action(0x00, sub.static_action_id);
  }
}

static void event_task(void* arg) {
  (void)arg;
  event_item_t item;
  while (1) {
    if (!R_QUEUE_RECEIVE(s_event_queue, &item, WAIT_FOREVER)) continue;
    uint32_t dropped = __atomic_exchange_n(&s_isr_dropped, 0, __ATOMIC_RELAXED);
    if (dropped) SE_RAISE(ERR_EVENT_QUEUE_FULL, dropped);
    if (item.from_isr) deliver_inline(&item.event);
    deliver_queued(&item.event);
  }
}

#undef OWNER
#define OWNER OWNER_SYS_EVENT_PUBLISH
err_h sys_event_publish(const sys_event_t* event) {
  if (xPortInIsrContext()) {
    if (event == NULL) return NULL;
    event_item_t item = {.event = *event, .from_isr = true};
    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(s_event_queue, &item, &woken) != pdTRUE) {
      __atomic_fetch_add(&s_isr_dropped, 1, __ATOMIC_RELAXED);
    }
    if (woken) portYIELD_FROM_ISR();
    return NULL;
  }

  SE_CHECK_NOT_NULL(event);
  if (event->hops >= CONFIG_SYS_EVENT_MAX_HOPS) {
    SE_FAIL(ERR_EVENT_LOOP, event->domain, event->device_id, event->channel, event->event);
  }
  deliver_inline(event);
  if (!has_queued_listener(event)) return NULL;
  event_item_t item = {.event = *event, .from_isr = false};
  if (R_QUEUE_SEND(s_event_queue, &item, NO_WAIT) != pdTRUE) {
    SE_FAIL(ERR_EVENT_QUEUE_FULL, 1);
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_EVENT_SUBSCRIBE
err_h sys_event_subscribe(const sys_event_subscription_t* sub, uint8_t* out_id) {
  SE_CHECK_NOT_NULL(sub);
  SE_CHECK_NOT_NULL(out_id);
  bool has_target = sub->handler || sub->route_mask || sub->static_action_id || sub->dynamic_action_id;
  bool inline_ok = !sub->inline_call || (sub->handler && !sub->route_mask && !sub->static_action_id && !sub->dynamic_action_id);
  bool routes_ok = (sub->route_mask >> CONFIG_SYS_EVENT_ROUTE_COUNT) == 0;
  if (!has_target || !inline_ok || !routes_ok || (sub->user && sub->handler)) {
    SE_FAIL(ERR_EVENT_SUB_INVALID, sub->inline_call, sub->handler != NULL, sub->route_mask);
  }

  bool stored = false;
  taskENTER_CRITICAL(&s_lock);
  for (uint8_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBSCRIPTIONS; i++) {
    if (s_used & (1u << i)) continue;
    s_table[i] = *sub;
    s_used |= 1u << i;
    *out_id = i;
    stored = true;
    break;
  }
  taskEXIT_CRITICAL(&s_lock);
  if (!stored) SE_FAIL(ERR_EVENT_TABLE_FULL, CONFIG_SYS_EVENT_MAX_SUBSCRIPTIONS);
  return NULL;
}

err_h sys_event_unsubscribe(uint8_t id, bool user) {
  if (user && id == SYS_EVENT_SUB_ALL_USER) {
    taskENTER_CRITICAL(&s_lock);
    for (uint8_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBSCRIPTIONS; i++) {
      if (s_table[i].user) s_used &= ~(1u << i);
    }
    taskEXIT_CRITICAL(&s_lock);
    return NULL;
  }
  if (id >= CONFIG_SYS_EVENT_MAX_SUBSCRIPTIONS) SE_FAIL(ERR_EVENT_SUB_NOT_FOUND, id);

  bool found = false;
  bool protected = false;
  taskENTER_CRITICAL(&s_lock);
  if (s_used & (1u << id)) {
    found = true;
    protected = user && !s_table[id].user;
    if (!protected) s_used &= ~(1u << id);
  }
  taskEXIT_CRITICAL(&s_lock);
  if (!found) SE_FAIL(ERR_EVENT_SUB_NOT_FOUND, id);
  if (protected) SE_FAIL(ERR_EVENT_SUB_PROTECTED, id);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_EVENT_REGISTER_ROUTE
err_h sys_event_register_route(uint8_t route_idx, sys_event_route_f fn) {
  SE_CHECK_IN_RANGE(route_idx, 0, CONFIG_SYS_EVENT_ROUTE_COUNT - 1);
  s_routes[route_idx] = fn;
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_EVENT_INIT
err_h sys_event_init(void) {
  if (s_event_task == NULL) {
    R_TASK_START_ON_CORE(s_event_task, event_task, NULL, CONFIG_SYS_EVENT_TASK_PRIO, 0);
    SE_CHECK_IF_ALLOCATED(s_event_task);
  }
  ESP_LOGI(TAG, "Event system initialized");
  return NULL;
}
