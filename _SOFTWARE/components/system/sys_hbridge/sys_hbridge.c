#include "sys_hbridge.h"
#include "sys_device.h"

#define OWNER OWNER_SYS_HBRIDGE_BASE

//@contract-features $SYS_DEVICE_CONTRACT_HBRIDGE
const char* const sys_hbridge_feature_names[] = {"set_mode",  "set_drive", "brake",          "coast",       "get_current_mA",
                                                 "set_current_limit_mA", "get_fault", "clear_fault", NULL};
_Static_assert(sizeof(sys_hbridge_feature_names) / sizeof(sys_hbridge_feature_names[0]) - 1 == sizeof(sys_hbridge_contract_t) / sizeof(void (*)(void)),
               "sys_hbridge_feature_names must list every sys_hbridge_contract_t member in order");

#define SYS_HBRIDGE_DISPATCH(device_id, func_name, ...) \
  SYS_DEV_DISPATCH(device_id, SYS_DEVICE_CONTRACT_HBRIDGE, sys_hbridge_contract_t, func_name, ##__VA_ARGS__)

#undef OWNER
#define OWNER OWNER_SYS_HBRIDGE_SET_MODE
err_h sys_hbridge_set_mode(uint8_t device_id, uint8_t channel, sys_hbridge_mode_e mode) {
  SYS_HBRIDGE_DISPATCH(device_id, set_mode, channel, mode);
}

#undef OWNER
#define OWNER OWNER_SYS_HBRIDGE_SET_DRIVE
err_h sys_hbridge_set_drive(uint8_t device_id, uint8_t channel, float magnitude) {
  SYS_HBRIDGE_DISPATCH(device_id, set_drive, channel, magnitude);
}

#undef OWNER
#define OWNER OWNER_SYS_HBRIDGE_BRAKE
err_h sys_hbridge_brake(uint8_t device_id, uint8_t channel) {
  SYS_HBRIDGE_DISPATCH(device_id, brake, channel);
}

#undef OWNER
#define OWNER OWNER_SYS_HBRIDGE_COAST
err_h sys_hbridge_coast(uint8_t device_id, uint8_t channel) {
  SYS_HBRIDGE_DISPATCH(device_id, coast, channel);
}

#undef OWNER
#define OWNER OWNER_SYS_HBRIDGE_GET_CURRENT
err_h sys_hbridge_get_current_mA(uint8_t device_id, uint8_t channel, int32_t* out_mA) {
  SYS_HBRIDGE_DISPATCH(device_id, get_current_mA, channel, out_mA);
}

#undef OWNER
#define OWNER OWNER_SYS_HBRIDGE_SET_CURRENT_LIMIT
err_h sys_hbridge_set_current_limit_mA(uint8_t device_id, uint8_t channel, uint32_t limit_mA) {
  SYS_HBRIDGE_DISPATCH(device_id, set_current_limit_mA, channel, limit_mA);
}

#undef OWNER
#define OWNER OWNER_SYS_HBRIDGE_GET_FAULT
err_h sys_hbridge_get_fault(uint8_t device_id, uint8_t channel, bool* out_fault, sys_hbridge_fault_reason_e* out_reason) {
  SYS_HBRIDGE_DISPATCH(device_id, get_fault, channel, out_fault, out_reason);
}

#undef OWNER
#define OWNER OWNER_SYS_HBRIDGE_CLEAR_FAULT
err_h sys_hbridge_clear_fault(uint8_t device_id, uint8_t channel) {
  SYS_HBRIDGE_DISPATCH(device_id, clear_fault, channel);
}

