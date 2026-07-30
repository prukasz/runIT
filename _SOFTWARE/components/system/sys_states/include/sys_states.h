#pragma once
#include "sys_error.h"

/**
 * @brief Base system-wide operating states.
 *
 * 'STATE'  Is collection of recorded actions that when executed feed interface with saved commands (actions)
 *  State can poses some harcoded predefined functions
 *  State '0' is Executed at boot as board default configuration
 * `SYS_STATE_MAX`: Indexing start for non-user states
 */
typedef enum sys_state_e {
  SYS_STATE_BOOT = 0,
  SYS_STATE_FROZEN,          /* sys_device_freeze_all() */
  SYS_STATE_RESUME,          /* sys_device_resume_all() + sys_device_sync_all() - normal operation */
  SYS_STATE_SUSPENDED,       /* sys_device_suspend_all() */
  SYS_STATE_RESET,           /* sys_device_reset_all() */
  SYS_STATE_HARD_RESET,      /* sys_device_uninstall_all() */
  SYS_STATE_ONLINE,          /* dummy - no action yet */
  SYS_STATE_OFFLINE,         /* dummy - no action yet */
  SYS_STATE_CONNECTION_LOST, /* dummy - no action yet */
  SYS_STATE_EMERGENCY,       /* placeholder - no response actions implemented yet */
  SYS_STATE_MAX,             /* boundary - first assignable custom state ID, not a valid state itself */
} sys_state_e;

/** @brief Max number of user-created custom states (see sys_states_create()). */
#define SYS_STATES_MAX_CUSTOM 16

/**
 * @brief One past the last valid state ID, built-in or custom.
 *
 * Sizes sys_actions' state->action bind map (SYS_ACTIONS_MAX_BOUND_PER_STATE
 * entries per ID, `SYS_STATES_ID_SPACE` IDs total) - see sys_actions_bind_state().
 */
#define SYS_STATES_ID_SPACE (SYS_STATE_MAX + SYS_STATES_MAX_CUSTOM)

/**
 * @brief Load persisted custom states from NVS. Call once at boot, before
 * any sys_states_create()/sys_states_enter() call for a custom state ID.
 *
 * @return err_h Status report (NULL on success).
 */
err_h sys_states_init(void);

/**
 * @brief Create a new custom state, persisted so it survives reboot.
 *
 * Assigns the next free ID at or above SYS_STATE_MAX and records it in the
 * custom-state bitmask in NVS so sys_states_enter(*out_state) keeps working
 * after a reboot without the caller re-creating it - only the caller's own
 * record of *which* ID it got needs to survive on its own (e.g. by creating
 * states in the same order at boot). There is no sys_states_remove(); slots are
 * only ever appended.
 *
 * @param out_state Set to the assigned state ID on success.
 * @return err_h NULL on success, ERR_NULL_PTR if out_state is NULL, or
 *               ERR_STATE_NO_CUSTOM_SLOTS if all SYS_STATES_MAX_CUSTOM slots are used.
 */
err_h sys_states_create(sys_state_e* out_state);

/**
 * @brief Run the action associated with `state` and, on success, make it current.
 *
 * For a built-in state (state < SYS_STATE_MAX), runs that state's own base
 * action first (see sys_states.c), then any sys_actions_bind_state()-bound
 * actions. For a custom state, there is no base action - only the bound
 * actions run, in bind order; entering one with nothing bound is a no-op
 * that still succeeds and becomes current.
 *
 * @param state Target state.
 * @return err_h NULL on success; ERR_INVALID_VAL_UI32 if state is outside
 *               0..SYS_STATES_ID_SPACE-1, ERR_STATE_UNKNOWN if it's an
 *               unused custom slot, ERR_BASE_NOT_SUPPORTED if a built-in
 *               state has no registered base action, or whatever the
 *               underlying action returns on failure (current state is left
 *               unchanged then).
 */
err_h sys_states_enter(sys_state_e state);

/** @brief The last state successfully entered; `SYS_STATE_NONE` before the first call. */
sys_state_e sys_states_get_current(void);

/** @brief Human-readable name for `state` (built-in name or "CUSTOM"), or "UNKNOWN" if out of range/unused. */
const char* sys_states_get_name(sys_state_e state);
