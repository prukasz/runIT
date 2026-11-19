#pragma once
#include <sdkconfig.h>
#include "sys_event.h"
#include "errors.h"

/*
Events bridge system callbacks into the running VM pass:
  sys_event task (Core 0) -> [ s_vm_event_q ] -> vm_event_drain() -> [ this pass's snapshot ] (Core 1)
Buffered as sys_event_t; valid for the duration of one scan cycle, lock-free to query.
*/

/** @brief Number of events in current cycle snapshot. */
uint8_t vm_event_count(void);

/** @brief Event at index i of this cycle's snapshot, or NULL if out of bounds. */
const sys_event_t* vm_event_at(uint8_t i);

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
