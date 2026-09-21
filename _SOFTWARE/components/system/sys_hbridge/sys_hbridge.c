#include "sys_hbridge.h"
#include <stdint.h>
#include "esp_log.h"
#include "sys_device.h"
#include "sys_error.h"

static const char* TAG = "SYS_HBRIDGE";
#define OWNER OWNER_SYS_ERRORS_BASE

const char* const sys_hbridge_feature_e_to_string[] = {
    "SET_MODE",
    "SET_DRIVE",
    "BRAKE",
    "COAST",
    "GET_CURRENT_MA",
    "SET_CURRENT_LIMIT_MA",
    "CONFIGURE_FAULT",
    "GET_FAULT",
    "CLEAR_FAULT",
};

#define SYS_HBRIDGE_DISPATCH(dev_id, func_name, feature_id, channel, ...)                                               \
  do {                                                                                                                  \
    sys_device_t* __disp_dev = sys_device_get_by_id((dev_id));                                                         \
    if (__disp_dev == NULL) {                                                                                           \
      SE_RET_ERR(ERR_DEV_NOT_FOUND, (dev_id));                                                                          \
    }                                                                                                                   \
    if (!SYS_DEV_IS_INSTALLED(__disp_dev)) {                                                                            \
      SE_RET_ERR(ERR_DEV_NOT_INSTALLED, (dev_id));                                                                      \
    }                                                                                                                   \
    if (SYS_DEV_IS_SUSPENDED(__disp_dev)) {                                                                             \
      SE_RET_ERR(ERR_DEV_SUSPENDED, (dev_id));                                                                          \
    }                                                                                                                   \
    IF_SYS_DEV_AND_FEATURE(dev_id, SYS_DEVICE_CONTRACT_HBRIDGE, sys_hbridge_vtable_t, func_name, dev_ptr, vtable_ptr) { \
      SE_PASS_ON_ERR(vtable_ptr->func_name(dev_ptr->device_handle, (channel), ##__VA_ARGS__),                          \
                     ERR_DEV_DEP_FAILED, (dev_id));                                                                     \
      return NULL;                                                                                                      \
    }                                                                                                                   \
    SE_RET_ERR(ERR_DEV_FEATURE_UNAVAILABLE, (dev_id), SYS_DEVICE_CONTRACT_HBRIDGE, (uint8_t)(feature_id));              \
  } while (0)

err_h sys_hbridge_set_mode(uint8_t device_id, uint8_t channel, sys_hbridge_mode_e mode) {
  SYS_HBRIDGE_DISPATCH(device_id, set_mode, SYS_HBRIDGE_FEATURE_SET_MODE, channel, mode);
}

err_h sys_hbridge_set_drive(uint8_t device_id, uint8_t channel, float magnitude) {
  SYS_HBRIDGE_DISPATCH(device_id, set_drive, SYS_HBRIDGE_FEATURE_SET_DRIVE, channel, magnitude);
}

err_h sys_hbridge_brake(uint8_t device_id, uint8_t channel) {
  SYS_HBRIDGE_DISPATCH(device_id, brake, SYS_HBRIDGE_FEATURE_BRAKE, channel);
}

err_h sys_hbridge_coast(uint8_t device_id, uint8_t channel) {
  SYS_HBRIDGE_DISPATCH(device_id, coast, SYS_HBRIDGE_FEATURE_COAST, channel);
}

err_h sys_hbridge_get_current_ma(uint8_t device_id, uint8_t channel, uint32_t* out_ma) {
  SYS_HBRIDGE_DISPATCH(device_id, get_current_ma, SYS_HBRIDGE_FEATURE_GET_CURRENT_MA, channel, out_ma);
}

err_h sys_hbridge_set_current_limit_ma(uint8_t device_id, uint8_t channel, uint32_t limit_ma) {
  SYS_HBRIDGE_DISPATCH(device_id, set_current_limit_ma, SYS_HBRIDGE_FEATURE_SET_CURRENT_LIMIT_MA, channel, limit_ma);
}

err_h sys_hbridge_configure_fault(uint8_t device_id, uint8_t channel, const sys_hbridge_fault_config_t* config) {
  SYS_HBRIDGE_DISPATCH(device_id, configure_fault, SYS_HBRIDGE_FEATURE_CONFIGURE_FAULT, channel, config);
}

err_h sys_hbridge_get_fault(uint8_t device_id, uint8_t channel, bool* out_fault, sys_hbridge_fault_reason_e* out_reason) {
  SYS_HBRIDGE_DISPATCH(device_id, get_fault, SYS_HBRIDGE_FEATURE_GET_FAULT, channel, out_fault, out_reason);
}

err_h sys_hbridge_clear_fault(uint8_t device_id, uint8_t channel) {
  SYS_HBRIDGE_DISPATCH(device_id, clear_fault, SYS_HBRIDGE_FEATURE_CLEAR_FAULT, channel);
}

