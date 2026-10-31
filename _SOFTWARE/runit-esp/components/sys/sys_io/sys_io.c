#include "sys_io.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_IO, "sys_io", ERR_SYS_IO_MAP)

err_h sys_io_set_mode(sys_io_pin_t p, sys_io_mode_e mode) {
  const io_mode_args_t a = {.pin = p.pin, .mode = (uint8_t)mode};
  return sys_device_write(p.device, IO_OP_MODE, &a, sizeof(a));
}

// A device without the op (the pins have a fixed function) is fine; any other failure is returned.
static err_h ignore_unknown_op(err_h err) {
  if (err_root_tag(err) == ERR_DEV_UNKNOWN_OP) {
    err_free(err);
    return NULL;
  }
  return err;
}

err_h sys_io_prepare(sys_io_pin_t p, sys_io_mode_e mode) {
  return ignore_unknown_op(sys_io_set_mode(p, mode));
}

err_h sys_io_prepare_input(sys_io_pin_t p) {
  err_h err = sys_io_set_mode(p, SYS_IO_MODE_INPUT_PULLUP);
  if (err_root_tag(err) == ERR_IO_MODE_UNSUPPORTED) {  // an expander without pull-ups: the board has an external one
    err_free(err);
    err = sys_io_set_mode(p, SYS_IO_MODE_INPUT);
  }
  return err;
}

err_h sys_io_reset(sys_io_pin_t p) {
  const io_pin_args_t a = {.pin = p.pin};
  return sys_device_write(p.device, IO_OP_RESET, &a, sizeof(a));
}

err_h sys_io_release(sys_io_pin_t p) {
  return ignore_unknown_op(sys_io_reset(p));
}

err_h sys_io_configure_intr(sys_io_pin_t p, const sys_io_intr_cfg_t* cfg) {
  ERR_CHECK(cfg != NULL, ERR_DEV_BAD_ARG);
  const io_intr_args_t a = {.pin = p.pin, .cfg = *cfg};
  return sys_device_write(p.device, IO_OP_INTR, &a, sizeof(a));
}

err_h sys_io_set_level(sys_io_pin_t p, bool level) {
  const io_level_args_t a = {.pin = p.pin, .level = level};
  return sys_device_write(p.device, IO_OP_LEVEL, &a, sizeof(a));
}

err_h sys_io_toggle(sys_io_pin_t p) {
  const io_pin_args_t a = {.pin = p.pin};
  return sys_device_write(p.device, IO_OP_TOGGLE, &a, sizeof(a));
}

err_h sys_io_get_level(sys_io_pin_t p, bool* level) {
  const io_pin_args_t a = {.pin = p.pin};
  io_level_args_t out = {0};
  ERR_CHECK(level != NULL, ERR_DEV_BAD_ARG);
  ERR_TRY(sys_device_read(p.device, IO_OP_LEVEL, &a, sizeof(a), &out, sizeof(out)));
  *level = out.level != 0;
  return NULL;
}

err_h sys_io_get_voltage(sys_io_pin_t p, int32_t* out_mV) {
  const io_pin_args_t a = {.pin = p.pin};
  io_voltage_args_t out = {0};
  ERR_CHECK(out_mV != NULL, ERR_DEV_BAD_ARG);
  ERR_TRY(sys_device_read(p.device, IO_OP_VOLTAGE, &a, sizeof(a), &out, sizeof(out)));
  *out_mV = out.voltage_mV;
  return NULL;
}

err_h sys_io_set_voltage(sys_io_pin_t p, int32_t voltage_mV) {
  const io_voltage_args_t a = {.pin = p.pin, .voltage_mV = voltage_mV};
  return sys_device_write(p.device, IO_OP_VOLTAGE, &a, sizeof(a));
}

err_h sys_io_set_pwm_duty(sys_io_pin_t p, uint32_t duty) {
  const io_duty_args_t a = {.pin = p.pin, .duty = duty};
  return sys_device_write(p.device, IO_OP_PWM_DUTY, &a, sizeof(a));
}

err_h sys_io_set_pwm_frequency(sys_io_pin_t p, uint32_t frequency_Hz) {
  const io_freq_args_t a = {.pin = p.pin, .frequency_Hz = frequency_Hz};
  return sys_device_write(p.device, IO_OP_PWM_FREQ, &a, sizeof(a));
}

err_h sys_io_reply_level(uint8_t pin, bool level, cmd_out_t* out) {
  const io_level_args_t a = {.pin = pin, .level = level};
  return cmd_reply(out, &a, sizeof(a));
}

err_h sys_io_reply_voltage(uint8_t pin, int32_t voltage_mV, cmd_out_t* out) {
  const io_voltage_args_t a = {.pin = pin, .voltage_mV = voltage_mV};
  return cmd_reply(out, &a, sizeof(a));
}
