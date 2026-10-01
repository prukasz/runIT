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

/* The body, called every pass. */
void vm_blk_on_event(vm_block_h b);

//#vm-block VM_BLK_ON_EVENT @id 15
//@title On Event
//@category system
//@activation enabled Runs every pass while enabled.
//@data vm_block_on_event_data_t
//@block-description Pulses ENO for one pass when a matching system event arrived (needs a subscription routed to the VM).
//@out 0 value @title Value @description The event's value (last match of the pass). @value i32
//@out 1 count @title Count @description Matches this pass. @value u32
