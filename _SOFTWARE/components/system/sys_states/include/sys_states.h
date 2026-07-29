#pragma once
#include "sys_error.h"

/**
 * @brief Base system-wide operating states.
 *
 * `sys_states_enter()` runs one static action per state (see sys_states.c)
 * and, only if that action succeeds, records `state` as current. States are
 * not mutually exclusive with `sys_device`'s own per-device state machine -
 * entering `SYS_STATE_FROZEN` simply calls `sys_device_freeze_all()` and does
 * not prevent a later, unrelated single-device suspend/resume call.
 *
 * `SYS_STATE_RESET` and `SYS_STATE_HARD_RESET` are one-shot sweeps (reset /
 * uninstall every device) but are represented as states like any other -
 * `sys_states` does not auto-transition back to `SYS_STATE_RESUME` afterward;
 * call `sys_states_enter(SYS_STATE_RESUME)` explicitly if that's the desired
 * next state.
 */
typedef enum sys_state_e {
  SYS_STATE_NONE = 0,
  SYS_STATE_FROZEN,          /* sys_device_freeze_all() */
  SYS_STATE_RESUME,          /* sys_device_resume_all() + sys_device_sync_all() - normal operation */
  SYS_STATE_SUSPENDED,       /* sys_device_suspend_all() */
  SYS_STATE_RESET,           /* sys_device_reset_all() */
  SYS_STATE_HARD_RESET,      /* sys_device_uninstall_all() */
  SYS_STATE_ONLINE,          /* dummy - no action yet */
  SYS_STATE_OFFLINE,         /* dummy - no action yet */
  SYS_STATE_CONNECTION_LOST, /* dummy - no action yet */
  SYS_STATE_EMERGENCY,       /* placeholder - no response actions implemented yet */
  SYS_STATE_MAX,
} sys_state_e;

/**
 * @brief Run the action associated with `state` and, on success, make it current.
 *
 * @param state Target state.
 * @return err_h NULL on success; `ERR_INVALID_VAL_UI32` if `state` is out of
 *               range, `ERR_BASE_NOT_SUPPORTED` if `state` has no registered
 *               action, or whatever the underlying `sys_device_*_all()` call
 *               returns on failure (current state is left unchanged then).
 */
err_h sys_states_enter(sys_state_e state);

/** @brief The last state successfully entered; `SYS_STATE_NONE` before the first call. */
sys_state_e sys_states_get_current(void);

/** @brief Human-readable name for `state`, or "UNKNOWN" if out of range. */
const char* sys_states_get_name(sys_state_e state);
