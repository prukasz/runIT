#include "sys_power_monitor.h"
#include "sys_power_vreg.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_POWER, "sys_power", ERR_SYS_POWER_MAP)

/* ---- POWER_MONITOR ---- */

// A channel reading: the three GET ops answer one i32.
static err_h monitor_read(uint16_t device, uint16_t op, uint8_t channel, int32_t* out) {
  const pmon_channel_args_t a = {.channel = channel};
  int32_t value = 0;
  uint32_t out_len = 0;
  ERR_CHECK(out != NULL, ERR_DEV_BAD_ARG);
  ERR_TRY(sys_device_call(device, op, &a, sizeof(a), &value, sizeof(value), &out_len));
  *out = value;
  return NULL;
}

err_h sys_power_monitor_get_voltage(uint16_t device, uint8_t channel, int32_t* out_mV) {
  return monitor_read(device, PMON_OP_GET_VOLTAGE, channel, out_mV);
}

err_h sys_power_monitor_get_current(uint16_t device, uint8_t channel, int32_t* out_mA) {
  return monitor_read(device, PMON_OP_GET_CURRENT, channel, out_mA);
}

err_h sys_power_monitor_get_power(uint16_t device, uint8_t channel, int32_t* out_mW) {
  return monitor_read(device, PMON_OP_GET_POWER, channel, out_mW);
}

err_h sys_power_monitor_set_alert(uint16_t device, uint8_t channel, sys_power_alert_e alert, int32_t threshold_mA) {
  const pmon_alert_args_t a = {.channel = channel, .alert = (uint8_t)alert, .threshold_mA = threshold_mA};
  return sys_device_call(device, PMON_OP_SET_ALERT, &a, sizeof(a), NULL, 0, NULL);
}

/* ---- POWER_VREG ---- */

err_h sys_power_vreg_set_enable(uint16_t device, bool enable) {
  const vreg_enable_args_t a = {.enable = enable};
  return sys_device_call(device, VREG_OP_SET_ENABLE, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_power_vreg_set_voltage(uint16_t device, uint32_t voltage_mV) {
  const vreg_voltage_args_t a = {.voltage_mV = voltage_mV};
  return sys_device_call(device, VREG_OP_SET_VOLTAGE, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_power_vreg_set_current(uint16_t device, uint32_t current_mA) {
  const vreg_current_args_t a = {.current_mA = current_mA};
  return sys_device_call(device, VREG_OP_SET_CURRENT, &a, sizeof(a), NULL, 0, NULL);
}
