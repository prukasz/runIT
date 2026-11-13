#include "sys_event.h"

#include <stddef.h>

#include "board_time.h"
#include "utils.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_EVENT, "event", ERR_SYS_EVENT_MAP)
MODULE_ID_DEFINE(SYS_EVENT, 0x25)

_Static_assert(CONFIG_SYS_EVENT_MAX_SUBS <= 255, "a subscription handle keeps the slot in a u8");

/* A subscription: which events, and a listener function called inline (in the publisher's task) or queued (in the
 * event task). A free slot has no function. `gen` counts the subscriptions the slot has had (1..255, never 0) and
 * stays when the slot is freed: a handle is [gen u8][slot u8], so the handle of a listener that is gone names no
 * listener, also after the slot was taken again, and handle 0 is never one. */
typedef struct {
  sys_event_key_t filter;
  sys_event_handler_f fn;
  void* ctx;
  bool inline_call;
  uint8_t gen;
} sub_t;

#define HANDLE(gen, slot) ((uint16_t)(((uint16_t)(gen) << 8) | (slot)))
#define HANDLE_SLOT(h)    ((uint8_t)((h) & 0xFFu))
#define HANDLE_GEN(h)     ((uint8_t)((h) >> 8))

#define EVENT_MAX_BYTES (sizeof(sys_event_t) + SYS_EVENT_DATA_MAX)

/* What the event task still has to do with a queued item: the queue's, never the event's flags. */
#define ITEM_INLINE 0x01u  // the inline listeners have not run yet (a deferred publish, an ISR)
#define ITEM_UPTIME 0x02u  // t_ms is an uptime reading (an ISR can't read the clock): the task turns it into the clock

/* An item as it is built and as it lies in the ring buffer: what is left to do, then the event and its payload (the
 * header ends in a flexible array, so the bytes are a plain array). The event starts at 4, so in an item at a 4 B
 * boundary (the stack, the ring) every field of it is aligned. */
typedef struct __attribute__((aligned(4))) {
  uint32_t todo;  // ITEM_*
  uint8_t event[EVENT_MAX_BYTES];
} item_t;

_Static_assert(offsetof(item_t, event) == 4, "the event at a 4 B boundary");

#define ITEM_SIZE(data_len) (offsetof(item_t, event) + sizeof(sys_event_t) + (data_len))

U_RINGBUFFER_DEFINE(sys_event_ring, CONFIG_SYS_EVENT_QUEUE_BYTES, RINGBUF_TYPE_NOSPLIT);

U_TASK_DEFINE(sys_event_task_h, CONFIG_SYS_EVENT_TASK_STACK);

static struct {
  sub_t subs[CONFIG_SYS_EVENT_MAX_SUBS];  // under s_lock
  /* ISR events the ring couldn't take. Counts up and wraps; only the ISR side writes it, the task reads it (an
     aligned u32 is read and written whole) and reports what it has not reported yet. */
  volatile uint32_t isr_drops;
  uint32_t isr_drops_reported;  // event task only
} s_event;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;  // s_event.subs: short copies only, never held while calling out

/* Per task: the hold its publishes go into (sys_event_hold), NULL when it holds nothing. Only a pointer lives here:
 * the room for the events is on the stack of the task that holds. */
static __thread sys_event_hold_t* s_hold;

#define PAD4(n) (((uint32_t)(n) + 3u) & ~3u)

/* ----------------------------------------------------------------------------
 * Matching and delivery
 * ------------------------------------------------------------------------- */

bool sys_event_match(const sys_event_key_t* filter, const sys_event_t* e) {
  const sys_event_key_t* k = &e->key;
  if (filter->source != SYS_EVENT_ANY_SOURCE && filter->source != k->source) {
    return false;
  }
  if (filter->channel != SYS_EVENT_ANY_CHANNEL && filter->channel != k->channel) {
    return false;
  }
  if (filter->tag == SYS_EVENT_ANY_TAG) {
    return true;
  }
  if (filter->tag == SYS_EVENT_ANY_NOTIFY) {
    return (e->flags & SYS_EVENT_NOTIFY) != 0u;
  }
  if (EVT_CODE(filter->tag) == SYS_EVENT_ANY_CODE) {
    return EVT_MODULE(filter->tag) == EVT_MODULE(k->tag);
  }
  return filter->tag == k->tag;
}

// A used slot of the kind asked for (inline or queued listeners) that matches the event.
static bool sub_matches(const sub_t* s, const sys_event_t* e, bool inline_call) {
  return s->fn != NULL && s->inline_call == inline_call && sys_event_match(&s->filter, e);
}

/* Calls the listeners of one kind (inline or queued) that match, in table order. Each slot is copied under the lock
 * and called outside it; one slot per lock, since a copy of the whole table would take CONFIG_SYS_EVENT_MAX_SUBS x
 * 21 B of the publisher's stack (an esp_timer task, a router task). A listener's failure is reported with the event;
 * the other listeners still run. */
static void run_listeners(const sys_event_t* e, bool inline_call) {
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBS; i++) {
    sub_t sub;
    portENTER_CRITICAL(&s_lock);
    const bool hit = sub_matches(&s_event.subs[i], e, inline_call);
    if (hit) {
      sub = s_event.subs[i];
    }
    portEXIT_CRITICAL(&s_lock);
    if (!hit) {
      continue;
    }
    err_h err = sub.fn(e, sub.ctx);
    if (err != NULL) {
      err_report(ERR_WRAP(err, ERR_EVENT_DELIVERY, .source = e->key.source, .event = e->key.tag,
                          .channel = e->key.channel));
    }
  }
}

static bool any_queued_listener(const sys_event_t* e) {
  bool any = false;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < CONFIG_SYS_EVENT_MAX_SUBS && !any; i++) {
    any = sub_matches(&s_event.subs[i], e, false);
  }
  portEXIT_CRITICAL(&s_lock);
  return any;
}

/* ----------------------------------------------------------------------------
 * Publish and the event task
 * ------------------------------------------------------------------------- */

// Keeps a stamped event in the task's hold: [size u32][the event, padded to 4 B]. false when it does not fit.
static bool hold_put(sys_event_hold_t* hold, const sys_event_t* ev) {
  const uint32_t size = (uint32_t)sizeof(sys_event_t) + ev->len;
  const uint32_t need = (uint32_t)sizeof(size) + PAD4(size);
  if (hold->cap - hold->used < need) {
    return false;
  }
  memcpy(hold->buf + hold->used, &size, sizeof(size));
  memcpy(hold->buf + hold->used + sizeof(size), ev, size);
  hold->used += need;
  return true;
}

/* The one way in. Here, in the publisher's task: the inline listeners, then one copy into the ring if a queued
 * listener matches; while the task holds its events (sys_event_hold), into the hold instead, for sys_event_release().
 * Deferred (SYS_EVENT_DEFER, or an ISR): all of it into the ring, for the event task. An ISR can't
 * read the clock (it takes a lock) nor report, so it stamps the uptime for the task to turn into the clock, and a drop
 * there is counted and the task reports it. */
err_h sys_event_publish(sys_event_key_t key, uint16_t flags, const void* data, uint32_t len) {
  const bool isr = xPortInIsrContext();
  const bool valid = len <= SYS_EVENT_DATA_MAX && (len == 0 || data != NULL);
  const bool defer = isr || (flags & SYS_EVENT_DEFER) != 0u;
  item_t item = {.todo = (defer ? ITEM_INLINE : 0u) | (isr ? ITEM_UPTIME : 0u)};
  sys_event_t* ev = (sys_event_t*)item.event;
  if (valid) {
    ev->key = key;
    ev->t_ms = isr ? board_time_uptime_ms() : board_time_now_ms();
    ev->flags = (uint16_t)(flags & SYS_EVENT_FLAGS_EVENT);
    ev->len = (uint16_t)len;
    if (len > 0) {
      memcpy(ev->data, data, len);
    }
  }

  if (isr) {
    BaseType_t woken = pdFALSE;
    if (!valid || xRingbufferSendFromISR(sys_event_ring, &item, ITEM_SIZE(len), &woken) != pdTRUE) {
      s_event.isr_drops++;
    }
    portYIELD_FROM_ISR(woken);
    return NULL;
  }

  ERR_CHECK(valid, ERR_EVENT_BAD_ARG);
  if (!defer && s_hold != NULL) {
    if (hold_put(s_hold, ev)) {
      return NULL;  // delivered by sys_event_release()
    }
    item.todo |= ITEM_INLINE;  // no room in the hold: all of it in the event task
  } else if (!defer) {
    run_listeners(ev, true);
    if (!any_queued_listener(ev)) {
      return NULL;
    }
  }
  ERR_CHECK(xRingbufferSend(sys_event_ring, &item, ITEM_SIZE(len), NO_WAIT) == pdTRUE, ERR_EVENT_QUEUE_FULL,
            .source = key.source, .event = key.tag, .dropped = 1);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Holding: a task's publishes kept until it let go of its locks
 * ------------------------------------------------------------------------- */

void sys_event_hold(sys_event_hold_t* hold, void* buf, uint32_t cap) {
  *hold = (sys_event_hold_t){0};
  if (s_hold != NULL || buf == NULL || xPortInIsrContext()) {
    return;  // an outer hold of this task keeps the events
  }
  hold->buf = buf;
  hold->cap = cap;
  s_hold = hold;
}

void sys_event_release(sys_event_hold_t* hold) {
  if (hold->buf == NULL || s_hold != hold) {
    return;  // it kept nothing
  }
  s_hold = NULL;  // first: a listener may hold and publish itself
  uint32_t at = 0;
  while (at < hold->used) {
    uint32_t size;
    memcpy(&size, hold->buf + at, sizeof(size));
    const sys_event_t* ev = (const sys_event_t*)(hold->buf + at + sizeof(size));  // at a 4 B boundary
    at += (uint32_t)sizeof(size) + PAD4(size);
    run_listeners(ev, true);
    if (any_queued_listener(ev)) {
      item_t item = {.todo = 0};
      memcpy(item.event, ev, size);
      if (xRingbufferSend(sys_event_ring, &item, ITEM_SIZE(ev->len), NO_WAIT) != pdTRUE) {
        err_report(ERR_NEW(ERR_EVENT_QUEUE_FULL, .source = ev->key.source, .event = ev->key.tag, .dropped = 1));
      }
    }
  }
  hold->used = 0;
}

static void event_task(void* arg) {
  (void)arg;
  while (1) {
    size_t size = 0;
    void* p = xRingbufferReceive(sys_event_ring, &size, portMAX_DELAY);
    if (p == NULL) {
      continue;
    }
    /* A copy, so the ring's room is free again before the listeners run (an I2C read can be slow). */
    item_t item;
    memcpy(&item, p, size);
    vRingbufferReturnItem(sys_event_ring, p);
    sys_event_t* ev = (sys_event_t*)item.event;
    if (item.todo & ITEM_UPTIME) {
      ev->t_ms = board_time_at_uptime_ms(ev->t_ms);
    }

    const uint32_t dropped = s_event.isr_drops - s_event.isr_drops_reported;  // unsigned: right across the wrap
    if (dropped > 0) {
      s_event.isr_drops_reported += dropped;
      err_report(ERR_NEW(ERR_EVENT_QUEUE_FULL, .source = SYS_EVENT_ANY_SOURCE, .event = SYS_EVENT_ANY_TAG,
                         .dropped = dropped));
    }
    if (item.todo & ITEM_INLINE) {
      run_listeners(ev, true);
    }
    run_listeners(ev, false);
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

err_h sys_event_subscribe(sys_event_key_t filter, sys_event_handler_f fn, void* ctx, sys_event_run_e run,
                          uint16_t* handle) {
  ERR_CHECK(fn != NULL && handle != NULL && run <= SYS_EVENT_RUN_FIRST, ERR_EVENT_BAD_ARG);
  sub_t sub = {.filter = filter, .fn = fn, .ctx = ctx, .inline_call = run != SYS_EVENT_RUN_QUEUED};
  /* Slot 0 is SYS_EVENT_RUN_FIRST's: the listeners run in table order, so it runs before every other. */
  const bool first = run == SYS_EVENT_RUN_FIRST;
  const uint32_t end = first ? 1u : CONFIG_SYS_EVENT_MAX_SUBS;
  bool added = false;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = first ? 0u : 1u; i < end && !added; i++) {
    if (s_event.subs[i].fn == NULL) {
      sub.gen = s_event.subs[i].gen == UINT8_MAX ? 1u : (uint8_t)(s_event.subs[i].gen + 1u);  // never 0
      s_event.subs[i] = sub;
      *handle = HANDLE(sub.gen, i);
      added = true;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(added, ERR_EVENT_TABLE_FULL);
  return NULL;
}

err_h sys_event_unsubscribe(uint16_t handle) {
  const uint8_t slot = HANDLE_SLOT(handle);
  bool found = false;
  portENTER_CRITICAL(&s_lock);
  if (slot < CONFIG_SYS_EVENT_MAX_SUBS && s_event.subs[slot].fn != NULL && s_event.subs[slot].gen == HANDLE_GEN(handle)) {
    s_event.subs[slot].fn = NULL;  // `gen` stays: the next listener of the slot gets another handle
    found = true;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(found, ERR_EVENT_NOT_FOUND, .id = handle);
  return NULL;
}
