#include "vm_event.h"
#include "utils.h"

#define OWNER OWNER_VM_EXEC

R_QUEUE_DEFINE(s_vm_event_q, CONFIG_VM_EVENT_DEPTH, sizeof(sys_event_t));

/* Scan cycle snapshot. Written by vm_event_drain(), read lock-free by supervisor/blocks. */
static sys_event_t s_cycle[CONFIG_VM_EVENT_DEPTH];
static uint8_t s_cycle_cnt;

static volatile uint16_t s_dropped;
static volatile uint16_t s_drop_domain;

bool vm_event_post(const sys_event_t* ev) {
  if (!ev) return false;

  if (R_QUEUE_SEND(s_vm_event_q, ev, NO_WAIT) == pdTRUE) return true;

  if (s_dropped != UINT16_MAX) s_dropped++;
  s_drop_domain = ev->domain;
  return false;
}

void vm_event_route(const sys_event_t* ev) {
  (void)vm_event_post(ev);
}

void vm_event_drain(void) {
  s_cycle_cnt = 0;
  while (s_cycle_cnt < CONFIG_VM_EVENT_DEPTH && R_QUEUE_RECEIVE(s_vm_event_q, &s_cycle[s_cycle_cnt], NO_WAIT)) {
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
  SE_FAIL(ERR_VM_EVENT_OVERFLOW, .domain = s_drop_domain, .depth = CONFIG_VM_EVENT_DEPTH, .dropped = dropped);
}

void vm_event_reset(void) {
  s_cycle_cnt = 0;
  xQueueReset(s_vm_event_q);
  s_dropped = 0;
}
