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

//@data vm_block_on_event_data_t
typedef struct __attribute__((aligned(4))) {
  uint8_t domain;     // @description Event domain, 255 = any @enum-ref sys_event_domain_e
  uint8_t device_id;  // @description Source device, 255 = any
  uint8_t channel;    // @description Pin / channel, 255 = any
  uint8_t event;      // @description Domain event, 255 = any
} vm_block_on_event_data_t;

_Static_assert(sizeof(vm_block_on_event_data_t) == 4, "vm_block_on_event_data_t must be 4 bytes");
_Static_assert(VM_EVENT_ANY == SYS_EVENT_ANY, "the wildcard must match sys_event's");
#define VM_ON_EVENT_CUSTOM_LEN sizeof(vm_block_on_event_data_t)

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's //@in / //@out says what the pin carries and
   which state field it replaces while unwired; titles and descriptions are in on_event.display.json. */
//#block-enum @alias On Event Outputs
typedef enum vm_out_on_event_e {
  VM_OUT_ON_EVENT_VALUE = 0,   //@out @value i32
  VM_OUT_ON_EVENT_COUNT,       //@out @value u32
} vm_out_on_event_e;

/* The body, called every pass.
   Face, titles and descriptions: on_event.display.json; the app's on_event.content.json is generated from this header. */
//#vm-block VM_BLK_ON_EVENT @id 15
//@activation enabled
void vm_blk_on_event(vm_block_h b);
