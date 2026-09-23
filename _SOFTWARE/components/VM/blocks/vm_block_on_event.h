#pragma once
#include "vm_block_helpers.h"
#include "vm_event.h"

/*
 *           -------------
 *  ->EN     |           | ->ENO
 *           | ON_EVENT  | ->VALUE
 *           |           | ->COUNT
 *           -------------
 *
 *  VM_BLK_ON_EVENT -- Starts a chain when a matching system event arrived.
 *
 *  - Looks through this pass's routed events (vm_event_count / vm_event_at):
 *    domain, device, channel and event must each match, or be VM_EVENT_ANY
 *    (255, same as SYS_EVENT_ANY). Events reach the VM only through a
 *    subscription that routes to it (class 0x09, route CONFIG_SYS_EVENT_ROUTE_VM);
 *    this block doesn't subscribe.
 *  - On a match: ENO pulses for this pass (loud), VALUE gets the event's value
 *    (converted to the output's type, loud), COUNT gets the number of matches
 *    this pass. Several matches in one pass: VALUE is the last one's.
 *  - Nothing is consumed: every ON_EVENT block sees every event of the pass.
 *    An event lives exactly one pass; one that arrives while the block is
 *    disabled is missed, like a physical edge.
 *  - Disabled: ENO drops quietly, events are ignored.
 *  - A one-pass pulse: to hold it (event -> wait -> act), put a LATCH below.
 *
 *  custom_data layout (4 bytes):
 *    [0] u8 domain     sys_event_domain_e, or 255 = any
 *    [1] u8 device_id  Source device, or 255 = any
 *    [2] u8 channel    Pin / channel, or 255 = any
 *    [3] u8 event      Domain event, or 255 = any
 */

#define VM_EVENT_ANY 0xFFu  // Same value as SYS_EVENT_ANY

typedef struct __attribute__((aligned(4))) {
  uint8_t domain;     // Event domain, 255 = any @enum-ref sys_event_domain_e
  uint8_t device_id;  // Source device, 255 = any
  uint8_t channel;    // Pin / channel, 255 = any
  uint8_t event;      // Domain event, 255 = any
} vm_block_on_event_data_t;

_Static_assert(sizeof(vm_block_on_event_data_t) == 4, "vm_block_on_event_data_t must be 4 bytes");
_Static_assert(VM_EVENT_ANY == SYS_EVENT_ANY, "the wildcard must match sys_event's");
#define VM_ON_EVENT_CUSTOM_LEN sizeof(vm_block_on_event_data_t)

#define VM_ON_EVENT_VALUE 0u
#define VM_ON_EVENT_COUNT 1u

static inline bool vm_on_event_field(uint8_t want, uint8_t got) {
  return want == VM_EVENT_ANY || want == got;
}

static inline bool vm_on_event_matches(const vm_block_on_event_data_t* f, const sys_event_t* ev) {
  return vm_on_event_field(f->domain, ev->domain) && vm_on_event_field(f->device_id, ev->device_id) &&
         vm_on_event_field(f->channel, ev->channel) && vm_on_event_field(f->event, ev->event);
}

/* Enable-driven: acts only on a pass that holds a matching event. */
static inline void vm_blk_on_event(vm_block_h b) {
  if (!vm_block_is_enabled(b)) {
    vm_block_set_eno(b, false);
    return;
  }

  vm_block_on_event_data_t filter;
  memcpy(&filter, vm_block_get_custom_data(b), sizeof(filter));

  uint32_t matches = 0;
  int32_t value = 0;
  const uint8_t n = vm_event_count();
  for (uint8_t i = 0; i < n; i++) {
    const sys_event_t* ev = vm_event_at(i);
    if (ev && vm_on_event_matches(&filter, ev)) {
      matches++;
      value = ev->value;
    }
  }

  if (matches == 0) {
    vm_block_set_eno(b, false);
    return;
  }

  if (b->cfg.q_cnt > VM_ON_EVENT_VALUE) {
    BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(value, vm_block_get_outputs(b)[VM_ON_EVENT_VALUE], 0), b);
  }
  if (b->cfg.q_cnt > VM_ON_EVENT_COUNT) {
    BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(matches, vm_block_get_outputs(b)[VM_ON_EVENT_COUNT], 0), b);
  }
  vm_block_set_eno(b, !vm_block_failed(b));
}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_ON_EVENT @title On Event @category system @state vm_block_on_event_data_t
//@block-description Pulses ENO for one pass when a matching system event arrived (needs a subscription routed to the VM).
//@out 0 value @title Value @description The event's value (last match of the pass).
//@out 1 count @title Count @description Matches this pass.
#define VM_BLOCK_TYPE_ON_EVENT \
  {.run = vm_blk_on_event, .check = NULL, .min_in = 0, .min_q = 0, .required_in = 0x0u, .state_len = VM_ON_EVENT_CUSTOM_LEN}
