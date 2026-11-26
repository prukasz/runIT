#include "vm_event.h"
#include <string.h>
#include "vm_errors.h"
#include "sys_device.h"
#include "utils.h"

U_QUEUE_DEFINE(s_vm_event_q, CONFIG_VM_EVENT_DEPTH, sizeof(vm_event_t));

static struct {
  vm_event_t cycle[CONFIG_VM_EVENT_DEPTH];  // this pass's events: VM task only, read lock-free by blocks
  uint8_t cycle_cnt;
  volatile uint32_t dropped;      // event task only writes (queue full)
  volatile uint16_t drop_source;  // event task only writes: the source of the last dropped event
  uint32_t dropped_reported;      // VM task only
  bool overflowing;               // VM task only: this run of drops was reported
  volatile bool wanted;           // the loaded program has an ON_EVENT block (vm_event_listen)
} s_vm_event;

/* The listener (queued: the event task) of the one wildcard subscription: every event goes to the queue, while the
   program has a block that reads it. Without one nothing is queued, so nothing is dropped and reported either (a safe
   stop suspends every device: an event each). */
static err_h on_event(const sys_event_t* ev, void* ctx) {
  (void)ctx;
  if (!s_vm_event.wanted) {
    return NULL;
  }
  vm_event_t e = {.key = ev->key};
  if (ev->len >= sizeof(e.value)) memcpy(&e.value, ev->data, sizeof(e.value));
  if (xQueueSend(s_vm_event_q, &e, NO_WAIT) != pdTRUE) {
    s_vm_event.drop_source = ev->key.source;
    s_vm_event.dropped++;
  }
  return NULL;
}

err_h vm_event_init(void) {
  uint16_t handle;
  const sys_event_key_t any = {.tag = SYS_EVENT_ANY_TAG, .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL};
  return sys_event_subscribe(any, on_event, NULL, SYS_EVENT_RUN_INLINE, &handle);
}

// The rule of a subscription (sys_event_match) on the event's key; the snapshot keeps no flags.
bool vm_event_matches(const sys_event_key_t* filter, const vm_event_t* ev) {
  const sys_event_t e = {.key = ev->key};
  return sys_event_match(filter, &e);
}

void vm_event_drain(void) {
  uint8_t n = 0;
  while (n < CONFIG_VM_EVENT_DEPTH && xQueueReceive(s_vm_event_q, &s_vm_event.cycle[n], NO_WAIT)) n++;
  s_vm_event.cycle_cnt = n;
}

uint8_t vm_event_count(void) {
  return s_vm_event.cycle_cnt;
}

const vm_event_t* vm_event_at(uint8_t i) {
  return (i < s_vm_event.cycle_cnt) ? &s_vm_event.cycle[i] : NULL;
}

void vm_event_listen(bool on) {
  s_vm_event.wanted = on;
}

err_h vm_event_take_overflow(void) {
  const uint32_t dropped = s_vm_event.dropped - s_vm_event.dropped_reported;  // unsigned: wrap-safe
  if (dropped == 0) {
    s_vm_event.overflowing = false;  // a pass with no drop ends the run
    return NULL;
  }
  s_vm_event.dropped_reported += dropped;
  if (s_vm_event.overflowing) return NULL;  // one report per run of drops, as a block's failures
  s_vm_event.overflowing = true;
  return ERR_NEW(ERR_VM_EVENT_OVERFLOW, .source = s_vm_event.drop_source, .depth = CONFIG_VM_EVENT_DEPTH,
                 .dropped = dropped > UINT16_MAX ? UINT16_MAX : (uint16_t)dropped);
}

void vm_event_reset(void) {
  s_vm_event.cycle_cnt = 0;
  xQueueReset(s_vm_event_q);
  s_vm_event.dropped_reported = s_vm_event.dropped;
  s_vm_event.overflowing = false;
  s_vm_event.wanted = false;  // the next program says again (its ON_EVENT blocks)
}
