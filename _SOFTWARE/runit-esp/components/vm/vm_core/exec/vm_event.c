#include "vm_event.h"
#include "vm_err.h"
#include "sys_device.h"
#include "utils.h"

U_QUEUE_DEFINE(s_vm_event_q, CONFIG_VM_EVENT_DEPTH, sizeof(sys_event_t));

static struct {
  sys_event_t cycle[CONFIG_VM_EVENT_DEPTH];  // this pass's events: VM task only, read lock-free by blocks
  uint8_t cycle_cnt;
  volatile uint32_t dropped;      // event task only writes (queue full)
  volatile uint16_t drop_source;  // event task only writes: the source of the last dropped event
  uint32_t dropped_reported;      // VM task only
} s_vm_event;

// The listener (queued: the event task) of the one wildcard subscription: every event goes to the queue.
static err_h on_event(const sys_event_t* ev, void* ctx) {
  (void)ctx;
  if (U_QUEUE_SEND(s_vm_event_q, ev, NO_WAIT) != pdTRUE) {
    s_vm_event.drop_source = ev->source;
    s_vm_event.dropped++;
  }
  return NULL;
}

err_h vm_event_init(void) {
  const sys_event_sub_t all = {.source = SYS_EVENT_ANY_SOURCE,
                               .event = SYS_EVENT_ANY_EVENT,
                               .channel = SYS_EVENT_ANY_CHANNEL,
                               .inline_call = false,
                               .device = SYS_DEVICE_ID_NONE,
                               .fn = on_event};
  uint8_t handle;
  return sys_event_subscribe(&all, &handle);
}

void vm_event_drain(void) {
  uint8_t n = 0;
  while (n < CONFIG_VM_EVENT_DEPTH && U_QUEUE_RECEIVE(s_vm_event_q, &s_vm_event.cycle[n], NO_WAIT)) n++;
  s_vm_event.cycle_cnt = n;
}

uint8_t vm_event_count(void) {
  return s_vm_event.cycle_cnt;
}

const sys_event_t* vm_event_at(uint8_t i) {
  return (i < s_vm_event.cycle_cnt) ? &s_vm_event.cycle[i] : NULL;
}

err_h vm_event_take_overflow(void) {
  const uint32_t dropped = s_vm_event.dropped - s_vm_event.dropped_reported;  // unsigned: wrap-safe
  if (dropped == 0) return NULL;
  s_vm_event.dropped_reported += dropped;
  return ERR_NEW(ERR_VM_EVENT_OVERFLOW, .source = s_vm_event.drop_source, .depth = CONFIG_VM_EVENT_DEPTH,
                 .dropped = dropped > UINT16_MAX ? UINT16_MAX : (uint16_t)dropped);
}

void vm_event_reset(void) {
  s_vm_event.cycle_cnt = 0;
  xQueueReset(s_vm_event_q);
  s_vm_event.dropped_reported = s_vm_event.dropped;
}
