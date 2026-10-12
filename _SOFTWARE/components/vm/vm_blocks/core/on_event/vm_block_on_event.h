#pragma once
#include "vm_block_helpers.h"
#include "vm_event.h"

/*
 *           -------------
 *  ->EN     |           | ->ENO
 *           | ON_EVENT  | ->VALUE
 *           -------------
 *
 *  VM_BLK_ON_EVENT -- Starts a chain when a matching system event arrived.
 *
 *  - Looks through this pass's events (vm_event_count / vm_event_at) with the match rule of a sys_event
 *    subscription (sys_event_matches): source, event and channel are each exact or a wildcard
 *    (SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_EVENT, SYS_EVENT_ANY_IN for "any event of that contract",
 *    SYS_EVENT_ANY_CHANNEL). The VM listens to every event (vm_event_init); this block doesn't subscribe.
 *  - On a match: ENO pulses for this pass (loud), VALUE gets the event's value
 *    (converted to the output's type, loud). Several matches in one pass
 *    collapse into one pulse: VALUE is the last one's.
 *  - Nothing is consumed: every ON_EVENT block sees every event of the pass.
 *    An event lives exactly one pass; one that arrives while the block is
 *    disabled is missed, like a physical edge.
 *  - Disabled: ENO drops quietly, events are ignored.
 *  - A one-pass pulse: to hold it (event -> wait -> act), put a LATCH below.
 */

typedef struct __attribute__((aligned(4))) {
  uint16_t source;  //@alias Device @description The device that raised the event (Any: every device) @id device @any 65535
  uint16_t event;  //@alias Event @description What happened (Any: every event of that device) @id event @device-field source @any 65535
  uint8_t channel;  //@alias Channel @description The pin or channel of that device the event came from (Any: every one) @id channel @device-field source @any 255
  uint8_t _pad[3];
} vm_block_on_event_data_t;

_Static_assert(sizeof(vm_block_on_event_data_t) == 8, "vm_block_on_event_data_t must be 8 bytes");
#define VM_ON_EVENT_CUSTOM_LEN sizeof(vm_block_on_event_data_t)

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's tags: @alias (the name a user sees), @value, ... (vm-block-annotations.md).
   An unwired pin uses the state field it @overrides. */
typedef enum vm_out_on_event_e {
  VM_OUT_ON_EVENT_VALUE = 0,  //@alias Value @description The event's value (last match of the pass). @value i32
} vm_out_on_event_e;

//#block VM_BLK_ON_EVENT
//@alias On Event
//@category system
//@description Pulses ENO for one pass when a matching system event arrived; the event's value goes to the output.
//@face On {event} {source}
//@activation enabled
//@when Runs every pass while enabled.
//@loop once
//@data vm_block_on_event_data_t
/* The body, called every pass. */
void vm_blk_on_event(vm_block_h b);
