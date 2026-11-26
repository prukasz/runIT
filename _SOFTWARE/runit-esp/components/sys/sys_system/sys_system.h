#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The board's system commands (MODULE_ID_SYS) and the two things they control: the safe state and the
 * dead-man (SYS_SYSTEM.MD). The safe state is the event SAFE (sys_system_events.h: the VM stops), then every device
 * suspended (sys_device_sweep); leaving it every device resumed.
 */

//#ref-enum @alias Safe State Reason
typedef enum {
  SYS_SAFE_STOP = 1,     //@alias Stop @description sys STOP command
  SYS_SAFE_DEADMAN = 2,  //@alias Deadman @description A client's session ended (silence past the dead-man's time, its link lost)
  SYS_SAFE_POWER = 3,    //@alias Power fault @description The power manager's response to a power fault (its response matrix: safe state)
  SYS_SAFE_FAULT = 4,    //@alias Device fault @description A device's critical error (the device error policy)
} sys_safe_reason_e;

/**
 * @brief Enters the safe state for `reason`, the one way into it: the reason and the time noted (SYS SAFE) until
 * sys_system_leave_safe_state, the event SAFE published inline (its listeners stop what writes to the devices), then
 * every device suspended (sys_device_sweep SUSPEND; a device's failure does not stop the others'). Not in a device's op
 * (a sweep takes every device's lock): a router task, the urgent task, the device policy's task, the power manager's.
 * @param[in] reason Why the board enters the safe state
 * @param[in] device The device whose error caused it (SAFE's source), SYS_DEVICE_ID_NONE when none
 * @return The sweep's error (the devices it reached are suspended all the same), or the publish's
 */
MUST_USE err_h sys_system_enter_safe_state(sys_safe_reason_e reason, uint8_t device);

/**
 * @brief Every device resumed (sys_device_sweep RESUME), then the note cleared; it stays when a device's resume fails.
 * @return The first device's error
 */
MUST_USE err_h sys_system_leave_safe_state(void);

/**
 * @brief The safe state as noted: the reason of the last entry (0 = not in it) and how long ago, in ms.
 * @param[out] reason sys_safe_reason_e of the last entry, 0: not in the safe state
 * @param[out] for_ms Milliseconds since the entry
 */
void sys_system_safe_state_get(uint8_t* reason, uint32_t* for_ms);

/**
 * @brief Sends this board's clock on every open session, so an event's t_ms compares across boards: when a client sets
 * it, when a peer is reached, and every CONFIG_SYS_SYSTEM_TIME_PERIOD_S. Nothing while no client has set the time. Any
 * task.
 */
void sys_system_broadcast_time(void);

/** @brief Restarts the board RESTART_DELAY_MS from now, so the answer of the command that asks leaves first. */
void sys_system_restart_later(void);

#ifdef __cplusplus
}
#endif
