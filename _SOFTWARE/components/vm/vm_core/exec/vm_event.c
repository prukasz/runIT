#include "vm_event.h"
#include "vm_err.h"
#include "sys_device.h"
#include "utils.h"

U_QUEUE_DEFINE(s_vm_event_q, CONFIG_VM_EVENT_DEPTH, sizeof(sys_event_t));

/* Scan cycle snapshot. Written by vm_event_drain(), read lock-free by supervisor/blocks. */
static sys_event_t s_cycle[CONFIG_VM_EVENT_DEPTH];
static uint8_t s_cycle_cnt;

static volatile uint16_t s_dropped;
static volatile uint16_t s_drop_source;

bool vm_event_post(const sys_event_t* ev) {
  if (!ev) return false;

  if (U_QUEUE_SEND(s_vm_event_q, ev, NO_WAIT) == pdTRUE) return true;

  if (s_dropped != UINT16_MAX) s_dropped++;
  s_drop_source = ev->source;
  return false;
}

// The listener (queued: the event task) of the one wildcard subscription: every event goes to the queue.
static err_h on_event(const sys_event_t* ev, void* ctx) {
  (void)ctx;
  (void)vm_event_post(ev);
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
  s_cycle_cnt = 0;
  while (s_cycle_cnt < CONFIG_VM_EVENT_DEPTH && U_QUEUE_RECEIVE(s_vm_event_q, &s_cycle[s_cycle_cnt], NO_WAIT)) {
    s_cycle_cnt++;
  }
}

uint8_t vm_event_count(void) {
  return s_cycle_cnt;
}

const sys_event_t* vm_event_snapshot(uint8_t* out_cnt) {
  if (out_cnt) *out_cnt = s_cycle_cnt;
  return s_cycle_cnt ? s_cycle : NULL;
}

const sys_event_t* vm_event_at(uint8_t i) {
  return (i < s_cycle_cnt) ? &s_cycle[i] : NULL;
}

err_h vm_event_take_overflow(void) {
  uint16_t dropped = s_dropped;
  if (dropped == 0) return NULL;
  s_dropped = 0;
  return ERR_NEW(ERR_VM_EVENT_OVERFLOW, .source = s_drop_source, .depth = CONFIG_VM_EVENT_DEPTH, .dropped = dropped);
}

void vm_event_reset(void) {
  s_cycle_cnt = 0;
  xQueueReset(s_vm_event_q);
  s_dropped = 0;
}
