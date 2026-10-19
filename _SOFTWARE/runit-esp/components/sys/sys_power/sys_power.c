#include "sys_power_monitor.h"
#include "sys_power_supply.h"
#include "sys_power_usb_pd.h"
#include "sys_power_vreg.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_POWER, "sys_power", ERR_SYS_POWER_MAP)

/* ---- POWER_MONITOR ---- */

// A channel reading: the three GET ops answer one i32.
static err_h monitor_read(uint16_t device, uint16_t op, uint8_t channel, int32_t* out) {
  const pmon_channel_args_t a = {.channel = channel};
  ERR_CHECK(out != NULL, ERR_DEV_BAD_ARG);
  return sys_device_read(device, op, &a, sizeof(a), out, sizeof(*out));
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

/* ---- POWER_SUPPLY ---- */

err_h sys_power_supply_select(uint16_t device, sys_power_supply_source_e source) {
  const supply_select_args_t a = {.source = (uint8_t)source};
  return sys_device_call(device, SUPPLY_OP_SELECT, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_power_supply_get_source(uint16_t device, sys_power_supply_source_e* out_source) {
  ERR_CHECK(out_source != NULL, ERR_DEV_BAD_ARG);
  uint8_t source = 0;
  ERR_TRY(sys_device_read(device, SUPPLY_OP_GET_SOURCE, NULL, 0, &source, sizeof(source)));
  *out_source = (sys_power_supply_source_e)source;
  return NULL;
}

/* ---- POWER_USB_PD ---- */

err_h sys_power_usb_pd_set_settings(uint16_t device, uint32_t voltage_mV, uint32_t current_mA) {
  const pd_settings_args_t a = {.voltage_mV = voltage_mV, .current_mA = current_mA};
  return sys_device_call(device, PD_OP_SET_SETTINGS, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_power_usb_pd_list_options(uint16_t device, sys_power_usb_pd_option_t* out, uint8_t max_options,
                                    uint8_t* out_count) {
  ERR_CHECK(out != NULL && out_count != NULL && max_options <= SYS_POWER_USB_PD_OPTIONS_MAX, ERR_DEV_BAD_ARG);
  sys_power_usb_pd_option_t all[SYS_POWER_USB_PD_OPTIONS_MAX];
  uint32_t len = 0;
  ERR_TRY(sys_device_call(device, PD_OP_LIST_OPTIONS, NULL, 0, all, sizeof(all), &len));
  ERR_CHECK(len % sizeof(all[0]) == 0, ERR_CMD_BAD_LEN, .got = (uint16_t)len, .need = (uint16_t)sizeof(all[0]));
  uint8_t count = (uint8_t)(len / sizeof(all[0]));
  count = count > max_options ? max_options : count;
  memcpy(out, all, count * sizeof(all[0]));
  *out_count = count;
  return NULL;
}

err_h sys_power_usb_pd_get_limits(uint16_t device, int32_t* out_mV, int32_t* out_mA) {
  ERR_CHECK(out_mV != NULL && out_mA != NULL, ERR_DEV_BAD_ARG);
  sys_power_usb_pd_limits_t limits;
  ERR_TRY(sys_device_read(device, PD_OP_GET_LIMITS, NULL, 0, &limits, sizeof(limits)));
  *out_mV = limits.voltage_mV;
  *out_mA = limits.current_mA;
  return NULL;
}
