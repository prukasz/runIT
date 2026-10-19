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
#define SYS_SYSTEM_OP_RESTART 0x03u  // restarts the board ~100 ms after the answer (the hard reset)

//#ref-enum @alias Safe State Reason
typedef enum {
  SYS_SAFE_STOP = 1,  //@alias Stop @description sys STOP command
  SYS_SAFE_DEADMAN = 2,  //@alias Deadman @description No frame within the dead-man timeout
  SYS_SAFE_LINK_LOST = 3,  //@alias Link lost @description The link lost its client with on_link_lost set
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
 * @brief Runtime dead-man. `timeout_ms` 0 = off. While on, the safe state is entered when no frame arrives from any link for
 * `timeout_ms` (once; the next frame re-arms it), and on sys_system_link_lost if `on_link_lost`. Not stored: after a
 * restart it is CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS again.
 */
void sys_system_deadman_set(uint32_t timeout_ms, bool on_link_lost);
void sys_system_deadman_get(uint32_t* timeout_ms, bool* on_link_lost);

/**
 * @brief A link lost its client (the app wires it: BLE disconnect). Enters the safe state when the dead-man is on with
 * `on_link_lost`. Any task, not an ISR.
 */
void sys_system_link_lost(void);

/**
 * @brief Watches the router's links (router_set_watch) and arms the dead-man with its boot default. Boot, before router_init.
 */
MUST_USE err_h sys_system_init(void);

#ifdef __cplusplus
}
#endif
