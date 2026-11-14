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
 * dead-man (SYS_SYSTEM.MD). The safe state is every device suspended (sys_device_sweep), leaving it every device
 * resumed.
 */

//#ref-enum @alias Safe State Reason
typedef enum {
  SYS_SAFE_STOP = 1,     //@alias Stop @description sys STOP command
  SYS_SAFE_DEADMAN = 2,  //@alias Deadman @description A client's session ended (silence past the dead-man's time, its link lost)
  // 3 is unused: never assign it.
  SYS_SAFE_POWER = 4,    //@alias Power fault @description The power manager's response to a power fault (its response matrix: safe state)
} sys_safe_reason_e;

/**
 * @brief Enters the safe state for `reason`: every device suspended (sys_device_sweep SUSPEND; a device's failure is
 * reported, the others are suspended all the same), and the reason and the time noted (SYS SAFE) until
 * sys_system_leave_safe_state. Not in a device's op (a sweep takes every device's lock): a router task, the urgent task.
 * @param[in] reason Why the board enters the safe state
 */
void sys_system_enter_safe_state(sys_safe_reason_e reason);

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
 * @brief Runtime dead-man: the silence of the master's and the clients' sessions. `timeout_ms` 0 = off (the sessions
 * keep CONFIG_SYS_SESSION_TIMEOUT_MS); else it is their timeout (sys_session_set_timeout), and such a session that
 * ends (silence that long, its link lost) enters the safe state. Not stored: after a restart it is
 * CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS again.
 * @param[in] timeout_ms 0: off, else SYS_SESSION_TIMEOUT_MIN_MS..SYS_SESSION_TIMEOUT_MAX_MS
 * @return NULL when set; ERR_CMD_BAD_ARG (not 0 and outside SYS_SESSION_TIMEOUT_MIN_MS..SYS_SESSION_TIMEOUT_MAX_MS)
 */
MUST_USE err_h sys_system_deadman_set(uint32_t timeout_ms);

/** @brief The dead-man's time in ms; 0 = off. */
uint32_t sys_system_deadman_get(void);

/**
 * @brief The root sends its clock to every board, so an event's t_ms compares across boards: when a client sets it,
 * when a board is reached, and every CONFIG_SYS_SYSTEM_TIME_PERIOD_S. Nothing on a board with a master, or while no
 * client has set the time. Any task.
 */
void sys_system_broadcast_time(void);

/** @brief Restarts the board RESTART_DELAY_MS from now, so the answer of the command that asks leaves first. */
void sys_system_restart_later(void);

/**
 * @brief Listens for gone peers (the dead-man) and reached ones (the time), starts the time broadcast's timer and arms
 * the dead-man with its boot default. Boot, after sys_session_init.
 * @return The error of the listeners or of the dead-man
 */
MUST_USE err_h sys_system_init(void);

#ifdef __cplusplus
}
#endif
