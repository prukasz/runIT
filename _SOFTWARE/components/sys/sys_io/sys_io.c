#include "sys_io.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_IO, "sys_io", ERR_SYS_IO_MAP)

err_h sys_io_set_mode(sys_io_pin_t p, sys_io_mode_e mode) {
  const io_mode_args_t a = {.pin = p.pin, .mode = (uint8_t)mode};
  return sys_device_call(p.device, IO_OP_SET_MODE, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_io_reset(sys_io_pin_t p) {
  const io_pin_args_t a = {.pin = p.pin};
  return sys_device_call(p.device, IO_OP_RESET, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_io_configure_intr(sys_io_pin_t p, const sys_io_intr_cfg_t* cfg) {
  ERR_CHECK(cfg != NULL, ERR_DEV_BAD_ARG);
  const io_intr_args_t a = {.pin = p.pin, .cfg = *cfg};
  return sys_device_call(p.device, IO_OP_CONFIGURE_INTR, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_io_set_level(sys_io_pin_t p, bool level) {
  const io_level_args_t a = {.pin = p.pin, .level = level};
  return sys_device_call(p.device, IO_OP_SET_LEVEL, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_io_toggle(sys_io_pin_t p) {
  const io_pin_args_t a = {.pin = p.pin};
  return sys_device_call(p.device, IO_OP_TOGGLE, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_io_get_level(sys_io_pin_t p, bool* level) {
  const io_pin_args_t a = {.pin = p.pin};
  uint8_t out = 0;
  uint32_t out_len = 0;
  ERR_CHECK(level != NULL, ERR_DEV_BAD_ARG);
  ERR_TRY(sys_device_call(p.device, IO_OP_GET_LEVEL, &a, sizeof(a), &out, sizeof(out), &out_len));
  *level = out != 0;
  return NULL;
}

err_h sys_io_get_voltage(sys_io_pin_t p, int32_t* out_mV) {
  const io_pin_args_t a = {.pin = p.pin};
  int32_t out = 0;
  uint32_t out_len = 0;
  ERR_CHECK(out_mV != NULL, ERR_DEV_BAD_ARG);
  ERR_TRY(sys_device_call(p.device, IO_OP_GET_VOLTAGE, &a, sizeof(a), &out, sizeof(out), &out_len));
  *out_mV = out;
  return NULL;
}

err_h sys_io_set_voltage(sys_io_pin_t p, int32_t voltage_mV) {
  const io_voltage_args_t a = {.pin = p.pin, .voltage_mV = voltage_mV};
  return sys_device_call(p.device, IO_OP_SET_VOLTAGE, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_io_set_pwm_duty(sys_io_pin_t p, uint32_t duty) {
  const io_duty_args_t a = {.pin = p.pin, .duty = duty};
  return sys_device_call(p.device, IO_OP_SET_PWM_DUTY, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_io_set_pwm_frequency(sys_io_pin_t p, uint32_t frequency_Hz) {
  const io_freq_args_t a = {.pin = p.pin, .frequency_Hz = frequency_Hz};
  return sys_device_call(p.device, IO_OP_SET_PWM_FREQ, &a, sizeof(a), NULL, 0, NULL);
}
