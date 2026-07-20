#include "sys_power.h"
#include <string.h>
#include "esp_log.h"
#include "status.h"
#include "sys_device.h"

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

uint32_t sys_power_get_limit_mv(void) { return s_power_limit_mv; }
uint32_t sys_power_get_limit_ma(void) { return s_power_limit_ma; }
uint32_t sys_power_get_budget_mw(void) { return s_power_budget_mw; }

status_rep_t sys_power_set_limits(uint32_t max_mv, uint32_t max_ma, uint32_t max_mw) {
  if (s_limits_locked) {
    return STA_C(ERR_INVALID_STATE, OWNER_SYS_POWER_BUDGET_UPDATE_SOURCE, 0, STATUS_PAYLOAD_UNKNOWN);
  }
  s_power_limit_mv = max_mv;
  s_power_limit_ma = max_ma;
  s_power_budget_mw = max_mw;
  s_limits_locked = true;
  return STA_OK;
}

/* ========================================================================== *
 * ZARZĄDZANIE BUDŻETEM (API SYSTEMOWE)
 * ========================================================================== */

#undef OWNER
#define OWNER OWNER_SYS_POWER_BUDGET_UPDATE_SOURCE
status_rep_t sys_power_budget_update_source(uint32_t max_mV, uint32_t max_mA) {
  s_budget.source_max_mV = max_mV;
  s_budget.source_max_mA = max_mA;

  s_budget.total_budget_mW = (max_mV * max_mA) / 1000;

  ESP_LOGI(TAG, "New Power Budget: %lu mW (%lu mV @ %lu mA)", s_budget.total_budget_mW, max_mV, max_mA);

  if (s_budget.allocated_mW > s_budget.total_budget_mW && !s_budget.ignore_limits) {
    ESP_LOGE(TAG, "CRITICAL: Allocated power (%lu mW) exceeds new budget!", s_budget.allocated_mW);
    return STA_C(ERR_POWER_BUDGET_EXCEEDED, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
  }

  return STA_OK;
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
 * REJESTRACJA KONTRAKTÓW
 * ========================================================================== */

#undef OWNER
#define OWNER OWNER_SYS_POWER_REGISTER_VREG
status_rep_t sys_power_register_vreg(uint8_t device_id, void* handle, const sys_power_vreg_contract* contract) {
  CHECK_ARG_RP(device_id, 0, MAX_DEVICE_ID + 1, 0);
  CHECK_NOT_NULL_RP(contract);

  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);

  dev->device_handle = handle;
  dev->contracts[SYS_DEVICE_CONTRACT_POWER_VREG] = (void*)contract;

  ESP_LOGI(TAG, "VREG registered for device_id: %u", device_id);
  return STA_OK;
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_REGISTER_MONITOR
status_rep_t sys_power_register_monitor(uint8_t device_id, void* handle, const sys_power_monitor_contract* contract) {
  CHECK_ARG_RP(device_id, 0, MAX_DEVICE_ID + 1, 0);
  CHECK_NOT_NULL_RP(contract);

  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);

  dev->device_handle = handle;
  dev->contracts[SYS_DEVICE_CONTRACT_POWER_MONITOR] = (void*)contract;

  ESP_LOGI(TAG, "Monitor registered for device_id: %u", device_id);
  return STA_OK;
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_REGISTER_USB_PD
status_rep_t sys_power_register_usb_pd(uint8_t device_id, void* handle, const sys_power_usb_pd_contract* contract) {
  CHECK_ARG_RP(device_id, 0, MAX_DEVICE_ID + 1, 0);
  CHECK_NOT_NULL_RP(contract);

  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);

  dev->device_handle = handle;
  dev->contracts[SYS_DEVICE_CONTRACT_POWER_USB_PD] = (void*)contract;

  ESP_LOGI(TAG, "USB PD registered for device_id: %u", device_id);
  return STA_OK;
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_UNREGISTER
status_rep_t sys_power_unregister(uint8_t device_id) {
  CHECK_ARG_RP(device_id, 0, MAX_DEVICE_ID + 1, 0);

  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (dev) {
    dev->contracts[SYS_DEVICE_CONTRACT_POWER_VREG] = NULL;
    dev->contracts[SYS_DEVICE_CONTRACT_POWER_MONITOR] = NULL;
    dev->contracts[SYS_DEVICE_CONTRACT_POWER_USB_PD] = NULL;
  }

  sys_power_device_t* p_dev = &s_power_registry[device_id];
  if (p_dev->allocated_mW > 0) {
    if (s_budget.allocated_mW >= p_dev->allocated_mW) {
      s_budget.allocated_mW -= p_dev->allocated_mW;
    } else {
      s_budget.allocated_mW = 0;
    }
  }

  memset(p_dev, 0, sizeof(sys_power_device_t));
  return STA_OK;
}

/* ========================================================================== *
 * VREG API
 * ========================================================================== */

#undef OWNER
#define OWNER OWNER_SYS_VREG_SET_ENABLE
status_rep_t sys_vreg_set_enable(uint8_t device_id, bool state) { SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_VREG, sys_power_vreg_contract, set_enable, state); }

#undef OWNER
#define OWNER OWNER_SYS_VREG_SET_VOLTAGE
status_rep_t sys_vreg_set_voltage(uint8_t device_id, uint32_t voltage_mV) {
  if (voltage_mV > SYS_POWER_LIMIT_MV) return STA_C(ERR_INVALID_ARG, OWNER, voltage_mV, STATUS_PAYLOAD_UNKNOWN);

  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (!dev->is_installed) return STA_C(ERR_DEV_NOT_INSTALLED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (dev->is_suspended) return STA_C(ERR_DEV_SUSPENDED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);

  IF_SYS_DEV_AND_FEATURE(device_id, SYS_DEVICE_CONTRACT_POWER_VREG, sys_power_vreg_contract, set_voltage, dev_ptr, vreg) {
    sys_power_device_t* p_dev = &s_power_registry[device_id];
    uint32_t requested_mW = (voltage_mV * p_dev->target_mA) / 1000;

    if (!s_budget.ignore_limits) {
      uint32_t projected_total_mW = (s_budget.allocated_mW - p_dev->allocated_mW) + requested_mW;
      if (projected_total_mW > s_budget.total_budget_mW) {
        ESP_LOGE(TAG, "VREG %u Budget Exceeded! Req: %lu mW", device_id, requested_mW);
        return STA_C(ERR_POWER_BUDGET_EXCEEDED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
      }
    }

    status_rep_t hw_status = vreg->set_voltage(dev->device_handle, voltage_mV);
    if (STA_IS_OK(hw_status)) {
      s_budget.allocated_mW = (s_budget.allocated_mW - p_dev->allocated_mW) + requested_mW;
      p_dev->target_mV = voltage_mV;
      p_dev->allocated_mW = requested_mW;
    }
    return hw_status;
  }

  return STA_C(ERR_NOT_SUPPORTED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
}

#undef OWNER
#define OWNER OWNER_SYS_VREG_SET_CURRENT
status_rep_t sys_vreg_set_current(uint8_t device_id, uint32_t current_mA) {
  if (current_mA > SYS_POWER_LIMIT_MA) return STA_C(ERR_INVALID_ARG, OWNER, current_mA, STATUS_PAYLOAD_UNKNOWN);

  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (!dev->is_installed) return STA_C(ERR_DEV_NOT_INSTALLED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (dev->is_suspended) return STA_C(ERR_DEV_SUSPENDED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);

  IF_SYS_DEV_AND_FEATURE(device_id, SYS_DEVICE_CONTRACT_POWER_VREG, sys_power_vreg_contract, set_current, dev_ptr, vreg) {
    sys_power_device_t* p_dev = &s_power_registry[device_id];
    uint32_t requested_mW = (p_dev->target_mV * current_mA) / 1000;

    if (!s_budget.ignore_limits) {
      uint32_t projected_total_mW = (s_budget.allocated_mW - p_dev->allocated_mW) + requested_mW;
      if (projected_total_mW > s_budget.total_budget_mW) {
        ESP_LOGE(TAG, "VREG %u Budget Exceeded! Req: %lu mW", device_id, requested_mW);
        return STA_C(ERR_POWER_BUDGET_EXCEEDED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
      }
    }

    status_rep_t hw_status = vreg->set_current(dev->device_handle, current_mA);
    if (STA_IS_OK(hw_status)) {
      s_budget.allocated_mW = (s_budget.allocated_mW - p_dev->allocated_mW) + requested_mW;
      p_dev->target_mA = current_mA;
      p_dev->allocated_mW = requested_mW;
    }
    return hw_status;
  }

  return STA_C(ERR_NOT_SUPPORTED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
}

#undef OWNER
#define OWNER OWNER_SYS_VREG_ADD_CALLBACK
status_rep_t sys_vreg_add_callback(uint8_t device_id, sys_power_events_e on_event, void (*callback)(uint8_t device_id, sys_power_events_e triggered_by)) { SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_VREG, sys_power_vreg_contract, add_callback, on_event, callback); }

/* ========================================================================== *
 * MONITOR API
 * ========================================================================== */

#undef OWNER
#define OWNER OWNER_SYS_POWER_MONITOR_GET_VOLTAGE
status_rep_t sys_power_monitor_get_voltage(uint8_t device_id, uint8_t channel, int32_t* out_mV) {
  CHECK_NOT_NULL_RP((void*)out_mV);
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_MONITOR, sys_power_monitor_contract, get_voltage, channel, out_mV);
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_MONITOR_GET_CURRENT
status_rep_t sys_power_monitor_get_current(uint8_t device_id, uint8_t channel, int32_t* out_mA) {
  CHECK_NOT_NULL_RP((void*)out_mA);
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_MONITOR, sys_power_monitor_contract, get_current, channel, out_mA);
}

#undef OWNER
#define OWNER OWNER_SYS_POWER_MONITOR_ADD_CALLBACK
status_rep_t sys_power_monitor_add_callback(uint8_t device_id, uint8_t channel, int32_t trigger_value, sys_power_events_e on_event, void (*callback)(uint8_t device_id, sys_power_events_e triggered_by)) {
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_MONITOR, sys_power_monitor_contract, add_callback, channel, trigger_value, on_event, callback);
}

/* ========================================================================== *
 * USB PD API
 * ========================================================================== */

#undef OWNER
#define OWNER OWNER_SYS_POWER_USB_PD_SET
status_rep_t sys_power_usb_pd_set(uint8_t device_id, uint32_t voltage_mV, uint32_t current_mA) { SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_USB_PD, sys_power_usb_pd_contract, set_settings, voltage_mV, current_mA); }

#undef OWNER
#define OWNER OWNER_SYS_POWER_USB_PD_LIST
status_rep_t sys_power_usb_pd_list(uint8_t device_id) { SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_POWER_USB_PD, sys_power_usb_pd_contract, list_options); }

#undef OWNER
#define OWNER OWNER_SYS_POWER_USB_PD_GET_LIMITS
status_rep_t sys_power_usb_pd_get_limits(uint8_t device_id, uint32_t* out_mV, uint32_t* out_mA) {
  CHECK_NOT_NULL_RP((void*)out_mV);
  CHECK_NOT_NULL_RP((void*)out_mA);

  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return STA_C(ERR_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (!dev->is_installed) return STA_C(ERR_DEV_NOT_INSTALLED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);
  if (dev->is_suspended) return STA_C(ERR_DEV_SUSPENDED, OWNER, DEV_ERR_PACK(device_id, 0, 0), STATUS_PAYLOAD_DEV_SOLO);

  IF_SYS_DEV_AND_FEATURE(device_id, SYS_DEVICE_CONTRACT_POWER_USB_PD, sys_power_usb_pd_contract, get_limits, dev_ptr, usb_pd) {
    status_rep_t stat = usb_pd->get_limits(dev_ptr->device_handle, out_mV, out_mA);
    if (STA_IS_OK(stat)) {
      sys_power_budget_update_source(*out_mV, *out_mA);
    }
    return stat;
  }
  return STA_C(ERR_NOT_SUPPORTED, OWNER, device_id, STATUS_PAYLOAD_UNKNOWN);
}
