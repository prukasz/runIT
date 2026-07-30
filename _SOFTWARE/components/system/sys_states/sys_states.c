#include "sys_states.h"
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>
#include "sys_actions.h"
#include "sys_device.h"
#include "utils.h"

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
// one built-in state maps to exactly one action. Custom states (>= SYS_STATE_MAX)
// have no base action at all - see sys_states_enter().
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

static const char* const s_state_names[SYS_STATE_MAX] = {
    [SYS_STATE_BOOT] = "BOOT",
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

static sys_state_e s_current_state = SYS_STATE_BOOT;

// ---------------------------------------------------------
// Custom states - user-created via sys_states_create(), persisted as a
// uint16_t bitmask (16 bits for SYS_STATES_MAX_CUSTOM = 16) in NVS.
// ---------------------------------------------------------

#define SYS_STATES_NVS_NAMESPACE "sys_states"
#define SYS_STATES_CUSTOM_KEY "custom"

// Guards s_custom_states_mask: sys_states_create() may be called from any task,
// concurrently with sys_states_enter()/get_name() reading it.
R_MUTEX_DEFINE(s_states_mutex);

static uint16_t s_custom_states_mask = 0;
static nvs_handle_t s_nvs = 0;

static err_h save_custom_states_locked(void) {
  SE_RET_IF_ESP_ERR(nvs_set_u16(s_nvs, SYS_STATES_CUSTOM_KEY, s_custom_states_mask));
  SE_RET_IF_ESP_ERR(nvs_commit(s_nvs));
  return NULL;
}

static err_h load_custom_states(void) {
  uint16_t mask = 0;
  esp_err_t rc = nvs_get_u16(s_nvs, SYS_STATES_CUSTOM_KEY, &mask);
  if (rc == ESP_ERR_NVS_NOT_FOUND) {
    s_custom_states_mask = 0;
    return NULL;
  }
  if (rc != ESP_OK) {
    SE_RET_ERR(ERR_ESP_ERR, .esp_code = rc);
  }
  s_custom_states_mask = mask;
  return NULL;
}

err_h sys_states_init(void) {
  SE_RET_IF_ESP_ERR(nvs_flash_init());
  SE_RET_IF_ESP_ERR(nvs_open(SYS_STATES_NVS_NAMESPACE, NVS_READWRITE, &s_nvs));
  SE_RET_IF_ERR(load_custom_states());
  ESP_LOGI(TAG, "sys_states initialized");
  return NULL;
}

err_h sys_states_create(sys_state_e* out_state) {
  SE_CHECK_NOT_NULL(out_state);

  R_MUTEX_LOCK(s_states_mutex, WAIT_FOREVER);

  int slot = -1;
  for (int i = 0; i < SYS_STATES_MAX_CUSTOM; i++) {
    if ((s_custom_states_mask & (1U << i)) == 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    R_MUTEX_UNLOCK(s_states_mutex);
    SE_RET_ERR(ERR_STATE_NO_CUSTOM_SLOTS, 0);
  }

  s_custom_states_mask |= (1U << slot);

  err_h err = save_custom_states_locked();
  if (SE_IS_ERR(err)) {
    s_custom_states_mask &= ~(1U << slot);
    R_MUTEX_UNLOCK(s_states_mutex);
    return err;
  }

  R_MUTEX_UNLOCK(s_states_mutex);

  *out_state = (sys_state_e)(SYS_STATE_MAX + slot);
  ESP_LOGI(TAG, "created custom state: id %u", (unsigned)*out_state);
  return NULL;
}

err_h sys_states_enter(sys_state_e state) {
  SE_CHECK_IN_RANGE((uint32_t)state, 0, (uint32_t)(SYS_STATES_ID_SPACE - 1));

  if (state < SYS_STATE_MAX) {
    sys_state_action_fn action = s_state_actions[state];
    if (!action) {
      SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, 0);
    }
    ESP_LOGI(TAG, "Entering system state: %s", sys_states_get_name(state));
    SE_RET_IF_ERR(action());
  } else {
    R_MUTEX_LOCK(s_states_mutex, WAIT_FOREVER);
    bool used = (s_custom_states_mask & (1U << (state - SYS_STATE_MAX))) != 0;
    R_MUTEX_UNLOCK(s_states_mutex);
    if (!used) {
      SE_RET_ERR(ERR_STATE_UNKNOWN, .state = (uint8_t)state);
    }
    ESP_LOGI(TAG, "Entering custom state: %u", (unsigned)state);
  }

  // Runs after the state's own base action (built-in states only), per
  // SYS_ACTIONS.MD - lets a bound action layer on top of (or, for a custom
  // state, entirely define) what happens without sys_states knowing anything
  // about what's bound.
  SE_RET_IF_ERR(sys_actions_invoke_for_state(state));

  s_current_state = state;
  return NULL;
}

sys_state_e sys_states_get_current(void) {
  return s_current_state;
}

const char* sys_states_get_name(sys_state_e state) {
  if (state < SYS_STATE_MAX) {
    return s_state_names[state] ? s_state_names[state] : "UNKNOWN";
  }
  if (state < SYS_STATES_ID_SPACE) {
    R_MUTEX_LOCK(s_states_mutex, WAIT_FOREVER);
    bool used = (s_custom_states_mask & (1U << (state - SYS_STATE_MAX))) != 0;
    R_MUTEX_UNLOCK(s_states_mutex);
    return used ? "CUSTOM" : "UNKNOWN";
  }
  return "UNKNOWN";
}
