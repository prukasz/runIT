#pragma once
#include <sdkconfig.h>
#include "sys_event.h"
#include "errors.h"

/*
Events bridge system callbacks into the running VM pass:
  the publisher's task (an inline listener) -> [ s_vm_event_q ] -> vm_event_drain() -> [ this pass's snapshot ] (VM task)
Buffered as vm_event_t (the key and the first 4 bytes of the payload); valid for the duration of one scan cycle, lock-free to query.
*/

/** @brief One event of the snapshot: its key and its value (the first 4 payload bytes, 0 when it has fewer). */
typedef struct {
  sys_event_key_t key;
  int32_t value;
} vm_event_t;

/** @brief Whether the event passes a filter of the ON_EVENT block, by the rule of a subscription (sys_event_match): the
 *  tag (exact, SYS_EVENT_ANY_TAG, or EVT_TAG(module, SYS_EVENT_ANY_CODE) for any event of a module), the source and
 *  the channel (each exact or SYS_EVENT_ANY_*). SYS_EVENT_ANY_NOTIFY matches nothing: the snapshot keeps no flags. */
bool vm_event_matches(const sys_event_key_t* filter, const vm_event_t* ev);

/** @brief Number of events in current cycle snapshot. */
uint8_t vm_event_count(void);

/** @brief Event at index i of this cycle's snapshot, or NULL if out of bounds. */
const vm_event_t* vm_event_at(uint8_t i);

/** @brief Drain incoming queue into the current cycle snapshot (called at start of pass). */
void vm_event_drain(void);

/** @brief Whether events are queued at all: on while the loaded program has an ON_EVENT block (the loader sets it). */
void vm_event_listen(bool on);

/** @brief ERR_VM_EVENT_OVERFLOW for the events dropped (queue full) since the last call, or NULL: once per run of passes
 *  with drops (a pass with none ends the run). VM task only. */
MUST_USE err_h vm_event_take_overflow(void);

/** @brief Empties the queue and the snapshot and stops listening; drops counted so far are not reported. VM task only. */
void vm_event_reset(void);

/**
 * @brief Subscribes the VM to every system event (one wildcard listener on the event task that fills the queue).
 * Boot step, after sys_event_init. Events are queued only while the program has an ON_EVENT block (vm_event_listen);
 * those blocks then match the per-pass snapshot by source, event and channel.
 */
MUST_USE err_h vm_event_init(void);
