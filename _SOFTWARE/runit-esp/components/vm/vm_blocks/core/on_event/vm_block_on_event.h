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
 *  - Its filter is a sys_event_key_t, matched against this pass's events (vm_event_count / vm_event_at) by the
 *    rule of a subscription (vm_event_matches): the tag (exact, SYS_EVENT_ANY_TAG, or EVT_TAG(module,
 *    SYS_EVENT_ANY_CODE) for any event of a contract), the source (SYS_EVENT_ANY_SOURCE) and the channel
 *    (SYS_EVENT_ANY_CHANNEL). The VM listens to every event (vm_event_init); this block doesn't subscribe.
 *  - On a match: ENO is on for this pass, VALUE gets the event's value
 *    (converted to the output's type). Several matches in one pass
 *    collapse into one pulse: VALUE is the last one's.
 *  - Nothing is consumed: every ON_EVENT block sees every event of the pass.
 *    An event lives exactly one pass; one that arrives while the block is
 *    disabled is missed, like a physical edge.
 *  - Disabled: ENO off, events are ignored.
 *  - A one-pass pulse: to hold it (event -> wait -> act), put a LATCH below.
 */

/* The filter, as a subscription's: tag = what happened (EVT_TAG(module, code); Any: every event), source = the device
 * that raised it (Any: every device), channel = its pin or channel (Any: every one). */
typedef sys_event_key_t vm_block_on_event_data_t;

_Static_assert(sizeof(vm_block_on_event_data_t) == 8, "vm_block_on_event_data_t must be 8 bytes");
#define VM_ON_EVENT_CUSTOM_LEN sizeof(vm_block_on_event_data_t)

typedef enum vm_out_on_event_e {
  VM_OUT_ON_EVENT_VALUE = 0,  // The event's value (last match of the pass).
} vm_out_on_event_e;

/* The body, called every pass. */
void vm_blk_on_event(vm_block_h b);
