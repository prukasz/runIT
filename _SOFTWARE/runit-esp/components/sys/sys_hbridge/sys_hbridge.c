#include "sys_hbridge.h"

err_h sys_hbridge_set_drive(uint16_t device, uint8_t channel, int16_t drive) {
  const hbridge_drive_args_t a = {.channel = channel, .drive = drive};
  return sys_device_call(device, HBRIDGE_OP_SET_DRIVE, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_hbridge_brake(uint16_t device, uint8_t channel) {
  const hbridge_channel_args_t a = {.channel = channel};
  return sys_device_call(device, HBRIDGE_OP_BRAKE, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_hbridge_coast(uint16_t device, uint8_t channel) {
  const hbridge_channel_args_t a = {.channel = channel};
  return sys_device_call(device, HBRIDGE_OP_COAST, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_hbridge_get_current_mA(uint16_t device, uint8_t channel, int32_t* out_mA) {
  const hbridge_channel_args_t a = {.channel = channel};
  ERR_CHECK(out_mA != NULL, ERR_DEV_BAD_ARG);
  return sys_device_read(device, HBRIDGE_OP_GET_CURRENT, &a, sizeof(a), out_mA, sizeof(*out_mA));
}
