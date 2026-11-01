#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The board's system commands (ROUTER_KEY_SYS) and the two things they
 * control: the safe state and the dead-man. What "safe" means is the app's
 * (VM stop, devices suspended): it registers the handler and the way out.
 */

/* Sys ops under ROUTER_KEY_SYS (sys_system_router.c). Never renumber. */
#define SYS_SYSTEM_OP_STOP    0x02u  // the safe state; the root's dead-man sends it to every board
#define SYS_SYSTEM_OP_RESTART 0x03u  // restarts the board ~100 ms after the answer (the hard reset)
#define SYS_SYSTEM_OP_TIME    0x07u  // the board's wall clock; the root sends its own to every board

//#ref-enum @alias Safe State Reason
typedef enum {
  SYS_SAFE_STOP = 1,  //@alias Stop @description sys STOP command
  SYS_SAFE_DEADMAN = 2,  //@alias Deadman @description A client's session ended (silence past the dead-man's time, its link lost)
  // 3 is unused: never assign it.
  SYS_SAFE_POWER = 4,  //@alias Power fault @description The power manager's response to a power fault (its response matrix: safe state)
} sys_safe_reason_e;

typedef void (*sys_safe_state_f)(sys_safe_reason_e reason);
typedef err_h (*sys_safe_leave_f)(void);

/**
 * @brief Sets the safe-state handler (runs on the caller's task: the router's urgent task for STOP, the dead-man and a lost
 * link). Boot only. NULL = only logged.
 */
void sys_system_set_safe_state(sys_safe_state_f fn);

/**
 * @brief Sets what leaving the safe state does (SYS SAFE_LEAVE): the devices resumed. Boot only. NULL: SAFE_LEAVE only clears
 * the note.
 */
void sys_system_set_safe_leave(sys_safe_leave_f fn);

/**
 * @brief Enters the safe state for `reason`: the handler runs and the reason and the time are noted (SYS SAFE_GET) until
 * sys_system_leave_safe_state.
 */
void sys_system_enter_safe_state(sys_safe_reason_e reason);

/**
 * @brief Runs the registered leave (the devices resumed), then clears the note; the note stays when the leave fails.
 */
MUST_USE err_h sys_system_leave_safe_state(void);

/**
 * @brief The safe state as noted: the reason of the last entry (0 = not in it) and how long ago, in ms.
 */
void sys_system_safe_state_get(uint8_t* reason, uint32_t* for_ms);

/**
 * @brief Runtime dead-man: the silence of the clients' sessions. `timeout_ms` 0 = off (the sessions keep
 * CONFIG_SYS_SESSION_TIMEOUT_MS); else it is their timeout (sys_session_set_timeout), and a client's session that
 * ends (silence that long, its link lost) enters the safe state. Not stored: after a restart it is
 * CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS again.
 * @return NULL when set; ERR_CMD_BAD_ARG (not 0 and outside SYS_SESSION_TIMEOUT_MIN_MS..SYS_SESSION_TIMEOUT_MAX_MS).
 */
MUST_USE err_h sys_system_deadman_set(uint32_t timeout_ms);

/** @brief The dead-man's time; 0 = off. */
uint32_t sys_system_deadman_get(void);

/**
 * @brief The root sends its clock to every board (SYS_SESSION.MD "Gone peers (and the dead-man)"), so an event's t_ms
 * compares across boards: when a client sets it, when a board is reached, and every
 * CONFIG_SYS_SESSION_TIME_PERIOD_S. Nothing on a board with a parent, or while no client has set the time. Any task.
 */
void sys_system_broadcast_time(void);

/**
 * @brief Listens for gone peers (sys_session_on_gone) and reached ones (the time), starts the time broadcast's timer
 * and arms the dead-man with its boot default. Boot, after sys_session_init.
 */
MUST_USE err_h sys_system_init(void);

#ifdef __cplusplus
}
#endif
