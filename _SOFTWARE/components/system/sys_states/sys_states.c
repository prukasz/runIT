#include "sys_states.h"
#include <esp_log.h>
#include "sys_actions.h"
#include "sys_device.h"

#define OWNER OWNER_SYS_STATES_BASE

static const char* TAG = __FILE_NAME__;

typedef err_h (*sys_state_action_fn)(void);

static err_h sys_states_action_frozen(void) {
  return sys_device_freeze_all();
}

static err_h sys_states_action_resume(void) {
  // Freeze and suspend are orthogonal in sys_device: only sync_all() clears a
  // freeze, only resume_all() clears a suspend. See SYS_DEVICE.MD.
  SE_RET_IF_ERR(sys_device_resume_all());
  SE_RET_IF_ERR(sys_device_sync_all());
  return NULL;
}

static err_h sys_states_action_suspended(void) {
  return sys_device_suspend_all();
}

static err_h sys_states_action_reset(void) {
  return sys_device_reset_all();
}

static err_h sys_states_action_hard_reset(void) {
  return sys_device_uninstall_all();
}

static err_h sys_states_action_online(void) {
  ESP_LOGI(TAG, "System state -> ONLINE (dummy, no action implemented)");
  return NULL;
}

static err_h sys_states_action_offline(void) {
  ESP_LOGI(TAG, "System state -> OFFLINE (dummy, no action implemented)");
  return NULL;
}

static err_h sys_states_action_connection_lost(void) {
  ESP_LOGW(TAG, "System state -> CONNECTION_LOST (dummy, no action implemented)");
  return NULL;
}

static err_h sys_states_action_emergency(void) {
  // TODO: real emergency response (e.g. freeze/suspend safety-critical devices).
  ESP_LOGE(TAG, "System state -> EMERGENCY (no response actions implemented yet)");
  return NULL;
}

// Direct index -> action (unlike callbacks' route table, which is bit-tested):
// one state maps to exactly one action. Empty slots (none currently) would be
// rejected by sys_states_enter() as ERR_BASE_NOT_SUPPORTED.
static const sys_state_action_fn s_state_actions[SYS_STATE_MAX] = {
    [SYS_STATE_FROZEN] = sys_states_action_frozen,
    [SYS_STATE_RESUME] = sys_states_action_resume,
    [SYS_STATE_SUSPENDED] = sys_states_action_suspended,
    [SYS_STATE_RESET] = sys_states_action_reset,
    [SYS_STATE_HARD_RESET] = sys_states_action_hard_reset,
    [SYS_STATE_ONLINE] = sys_states_action_online,
    [SYS_STATE_OFFLINE] = sys_states_action_offline,
    [SYS_STATE_CONNECTION_LOST] = sys_states_action_connection_lost,
    [SYS_STATE_EMERGENCY] = sys_states_action_emergency,
};

static sys_state_e s_current_state = SYS_STATE_NONE;

err_h sys_states_enter(sys_state_e state) {
  SE_CHECK_IN_RANGE((uint32_t)state, 0, (uint32_t)(SYS_STATE_MAX - 1));

  sys_state_action_fn action = s_state_actions[state];
  if (!action) {
    SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, 0);
  }

  ESP_LOGI(TAG, "Entering system state: %s", sys_states_get_name(state));
  SE_RET_IF_ERR(action());

  // Runs after the state's own base action, per SYS_ACTIONS.MD - lets a bound
  // action (e.g. "disable this regulator") layer on top of a generic sweep
  // (e.g. EMERGENCY's action being a no-op today) without sys_states knowing
  // anything about what's bound.
  SE_RET_IF_ERR(sys_actions_invoke_for_state(state));

  s_current_state = state;
  return NULL;
}

sys_state_e sys_states_get_current(void) {
  return s_current_state;
}

static const char* const s_state_names[SYS_STATE_MAX] = {
    [SYS_STATE_NONE] = "NONE",
    [SYS_STATE_FROZEN] = "FROZEN",
    [SYS_STATE_RESUME] = "RESUME",
    [SYS_STATE_SUSPENDED] = "SUSPENDED",
    [SYS_STATE_RESET] = "RESET",
    [SYS_STATE_HARD_RESET] = "HARD_RESET",
    [SYS_STATE_ONLINE] = "ONLINE",
    [SYS_STATE_OFFLINE] = "OFFLINE",
    [SYS_STATE_CONNECTION_LOST] = "CONNECTION_LOST",
    [SYS_STATE_EMERGENCY] = "EMERGENCY",
};

const char* sys_states_get_name(sys_state_e state) {
  return (state < SYS_STATE_MAX && s_state_names[state]) ? s_state_names[state] : "UNKNOWN";
}
