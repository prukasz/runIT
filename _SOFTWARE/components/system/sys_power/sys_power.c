#include "sys_power.h"
#include <string.h>
#include "esp_log.h"
#include "sys_device.h"
#include "sys_error.h"

static const char* TAG = __FILE_NAME__;

/* ========================================================================== *
 * WEWNĘTRZNE STRUKTURY STANU
 * ========================================================================== */

typedef struct {
  uint32_t source_max_mV;
  uint32_t source_max_mA;
  uint32_t total_budget_mW;
  uint32_t allocated_mW;
  bool ignore_limits;
} sys_power_budget_t;

typedef struct {
  uint32_t target_mV;
  uint32_t target_mA;
  uint32_t allocated_mW;
} sys_power_device_t;

static sys_power_budget_t s_budget = {0};
static sys_power_device_t s_power_registry[MAX_DEVICE_ID + 1] = {0};

static uint32_t s_power_limit_mv = 21000;
static uint32_t s_power_limit_ma = 5500;
static uint32_t s_power_budget_mw = 115500;
static bool s_limits_locked = false;

uint32_t sys_power_get_limit_mv(void) {
  return s_power_limit_mv;
}
uint32_t sys_power_get_limit_ma(void) {
  return s_power_limit_ma;
}
uint32_t sys_power_get_budget_mw(void) {
  return s_power_budget_mw;
}

#define OWNER OWNER_SYS_POWER_BASE
err_h sys_power_set_limits(uint32_t max_mv, uint32_t max_ma, uint32_t max_mw) {
  if (s_limits_locked) {
    SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
  }
  s_power_limit_mv = max_mv;
  s_power_limit_ma = max_ma;
  s_power_budget_mw = max_mw;
  s_limits_locked = true;
  return NULL;
}

/* ========================================================================== *
 * ZARZĄDZANIE BUDŻETEM (API SYSTEMOWE)
 * ========================================================================== */

#undef OWNER
#define OWNER OWNER_SYS_POWER_BUDGET_UPDATE_SOURCE
err_h sys_power_budget_update_source(uint32_t max_mV, uint32_t max_mA) {
  s_budget.source_max_mV = max_mV;
  s_budget.source_max_mA = max_mA;

  s_budget.total_budget_mW = (max_mV * max_mA) / 1000;

  ESP_LOGI(TAG, "New Power Budget: %lu mW (%lu mV @ %lu mA)", s_budget.total_budget_mW, max_mV, max_mA);

  if (s_budget.allocated_mW > s_budget.total_budget_mW && !s_budget.ignore_limits) {
    ESP_LOGE(TAG, "CRITICAL: Allocated power (%lu mW) exceeds new budget!", s_budget.allocated_mW);
    SE_RET_ERR(ERR_POWER_BUDGET_EXCEEDED, 0);
  }

  return NULL;
}

void sys_power_budget_set_ignore(bool ignore) {
  s_budget.ignore_limits = ignore;
  ESP_LOGW(TAG, "Power budget limits %s", ignore ? "IGNORED" : "ENFORCED");
}

void sys_power_budget_reset(void) {
  s_budget.source_max_mV = 0;
  s_budget.source_max_mA = 0;
  s_budget.total_budget_mW = 0;
  s_budget.allocated_mW = 0;

  for (int i = 0; i <= MAX_DEVICE_ID; i++) {
    s_power_registry[i].target_mV = 0;
    s_power_registry[i].target_mA = 0;
    s_power_registry[i].allocated_mW = 0;
  }
  ESP_LOGI(TAG, "Power budget fully reset.");
}

/* ========================================================================== *
 * VREG API
 * ========================================================================== */

#undef OWNER
#define OWNER OWNER_SYS_VREG_SET_ENABLE
err_h sys_vreg_set_enable(uint8_t device_id, bool state) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_VREG, sys_power_vreg_contract, set_enable, state);
}

#undef OWNER
#define OWNER OWNER_SYS_VREG_SET_VOLTAGE
err_h sys_vreg_set_voltage(uint8_t device_id, uint32_t voltage_mV) {
  SE_CHECK_IN_RANGE(voltage_mV, 0, SYS_POWER_LIMIT_MV);

  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) SE_RET_ERR(ERR_DEV_NOT_FOUND, device_id);
  if (!SYS_DEV_IS_INSTALLED(dev)) SE_RET_ERR(ERR_DEV_NOT_INSTALLED, device_id);
  if (SYS_DEV_IS_SUSPENDED(dev)) SE_RET_ERR(ERR_DEV_SUSPENDED, device_id);

  IF_SYS_DEV_AND_FEATURE(device_id, SYS_DEVICE_CONTRACT_POWER_VREG, sys_power_vreg_contract, set_voltage, dev_ptr, vreg) {
    sys_power_device_t* p_dev = &s_power_registry[device_id];
    uint32_t requested_mW = (voltage_mV * p_dev->target_mA) / 1000;

    if (!s_budget.ignore_limits) {
      uint32_t projected_total_mW = (s_budget.allocated_mW - p_dev->allocated_mW) + requested_mW;
      if (projected_total_mW > s_budget.total_budget_mW) {
        ESP_LOGE(TAG, "VREG %u Budget Exceeded! Req: %lu mW", device_id, requested_mW);
        SE_RET_ERR(ERR_POWER_BUDGET_EXCEEDED, device_id);
      }
    }

    err_h hw_status = vreg->set_voltage(dev->device_handle, voltage_mV);
    if (hw_status == NULL) {
      s_budget.allocated_mW = (s_budget.allocated_mW - p_dev->allocated_mW) + requested_mW;
      p_dev->target_mV = voltage_mV;
      p_dev->allocated_mW = requested_mW;
      return NULL;
    }
    SE_RET_IF_ERR(hw_status);
  }

  SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, 0);
}

#undef OWNER
#define OWNER OWNER_SYS_VREG_SET_CURRENT
err_h sys_vreg_set_current(uint8_t device_id, uint32_t current_mA) {
  SE_CHECK_IN_RANGE(current_mA, 0, SYS_POWER_LIMIT_MA);

  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) SE_RET_ERR(ERR_DEV_NOT_FOUND, device_id);
  if (!SYS_DEV_IS_INSTALLED(dev)) SE_RET_ERR(ERR_DEV_NOT_INSTALLED, device_id);
  if (SYS_DEV_IS_SUSPENDED(dev)) SE_RET_ERR(ERR_DEV_SUSPENDED, device_id);

  IF_SYS_DEV_AND_FEATURE(device_id, SYS_DEVICE_CONTRACT_POWER_VREG, sys_power_vreg_contract, set_current, dev_ptr, vreg) {
    sys_power_device_t* p_dev = &s_power_registry[device_id];
    uint32_t requested_mW = (p_dev->target_mV * current_mA) / 1000;

    if (!s_budget.ignore_limits) {
      uint32_t projected_total_mW = (s_budget.allocated_mW - p_dev->allocated_mW) + requested_mW;
      if (projected_total_mW > s_budget.total_budget_mW) {
        ESP_LOGE(TAG, "VREG %u Budget Exceeded! Req: %lu mW", device_id, requested_mW);
        SE_RET_ERR(ERR_POWER_BUDGET_EXCEEDED, device_id);
      }
    }

    err_h hw_status = vreg->set_current(dev->device_handle, current_mA);
    if (hw_status == NULL) {
      s_budget.allocated_mW = (s_budget.allocated_mW - p_dev->allocated_mW) + requested_mW;
      p_dev->target_mA = current_mA;
      p_dev->allocated_mW = requested_mW;
      return NULL;
    }
    SE_RET_IF_ERR(hw_status);
  }

  SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, 0);
}

#undef OWNER
#define OWNER OWNER_SYS_VREG_ADD_CALLBACK
err_h sys_vreg_add_callback(uint8_t device_id, sys_power_events_e on_event, uint16_t route_mask) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_VREG, sys_power_vreg_contract, add_callback, on_event, route_mask);
}

/* ========================================================================== *
 * MONITOR API
 * ========================================================================== */

#undef OWNER
#define OWNER OWNER_SYS_POWER_MONITOR_GET_VOLTAGE
err_h sys_power_monitor_get_voltage(uint8_t device_id, uint8_t channel, int32_t* out_mV) {
  SE_CHECK_NOT_NULL(out_mV);
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_MONITOR, sys_power_monitor_contract, get_voltage, channel, out_mV);
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_MONITOR_GET_CURRENT
err_h sys_power_monitor_get_current(uint8_t device_id, uint8_t channel, int32_t* out_mA) {
  SE_CHECK_NOT_NULL(out_mA);
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_MONITOR, sys_power_monitor_contract, get_current, channel, out_mA);
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_MONITOR_ADD_CALLBACK
err_h sys_power_monitor_add_callback(uint8_t device_id, uint8_t channel, int32_t trigger_value, sys_power_events_e on_event, uint16_t route_mask) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_MONITOR, sys_power_monitor_contract, add_callback, channel, trigger_value, on_event, route_mask);
}

/* ========================================================================== *
 * USB PD API
 * ========================================================================== */

#undef OWNER
#define OWNER OWNER_SYS_POWER_USB_PD_SET
err_h sys_power_usb_pd_set(uint8_t device_id, uint32_t voltage_mV, uint32_t current_mA) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_USB_PD, sys_power_usb_pd_contract, set_settings, voltage_mV, current_mA);
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_USB_PD_LIST
err_h sys_power_usb_pd_list(uint8_t device_id) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_USB_PD, sys_power_usb_pd_contract, list_options);
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_USB_PD_GET_LIMITS
err_h sys_power_usb_pd_get_limits(uint8_t device_id, uint32_t* out_mV, uint32_t* out_mA) {
  SE_CHECK_NOT_NULL(out_mV);
  SE_CHECK_NOT_NULL(out_mA);

  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) SE_RET_ERR(ERR_DEV_NOT_FOUND, device_id);
  if (!SYS_DEV_IS_INSTALLED(dev)) SE_RET_ERR(ERR_DEV_NOT_INSTALLED, device_id);
  if (SYS_DEV_IS_SUSPENDED(dev)) SE_RET_ERR(ERR_DEV_SUSPENDED, device_id);

  IF_SYS_DEV_AND_FEATURE(device_id, SYS_DEVICE_CONTRACT_POWER_USB_PD, sys_power_usb_pd_contract, get_limits, dev_ptr, usb_pd) {
    err_h err = usb_pd->get_limits(dev_ptr->device_handle, out_mV, out_mA);
    if (SE_IS_OK(err)) {
      sys_power_budget_update_source(*out_mV, *out_mA);
      return NULL;
    }
    SE_RET_IF_ERR(err);
  }
  SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, 0);
}
