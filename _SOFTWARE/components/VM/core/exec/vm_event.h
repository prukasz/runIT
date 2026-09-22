#pragma once
#include <sdkconfig.h>
#include "sys_event.h"
#include "sys_error.h"

/*
Events bridge system callbacks into the running VM pass:
  sys_event task (Core 0) -> [ s_vm_event_q ] -> vm_event_drain() -> [ s_cycle snapshot ] (Core 1)
Buffered as sys_event_t; valid for the duration of one scan cycle, lock-free to query.
*/

/** @brief Post an incoming callback event to the VM event queue (non-blocking). */
bool vm_event_post(const sys_event_t* ev);

/** @brief Number of events in current cycle snapshot. */
uint8_t vm_event_count(void);

/** @brief Direct pointer to this cycle's snapshot array. */
const sys_event_t* vm_event_snapshot(uint8_t* out_cnt);

/** @brief Event at index i of this cycle's snapshot, or NULL if out of bounds. */
const sys_event_t* vm_event_at(uint8_t i);

/** @brief Drain incoming queue into the current cycle snapshot (called at start of pass). */
void vm_event_drain(void);

/** @brief Fetch and clear any queue overflow error. */
SE_MUST_USE err_h vm_event_take_overflow(void);

/** @brief Reset incoming queue and cycle snapshot. */
void vm_event_reset(void);

/** @brief Event route for the VM (register at CONFIG_SYS_EVENT_ROUTE_VM; runit does). */
void vm_event_route(const sys_event_t* ev);
