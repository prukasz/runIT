#include "sys_io.h"

ERR_MODULE_DEFINE(MODULE_ID_IO, ERR_SYS_IO_MAP)

// A device without the op (the pins have a fixed function) is fine; any other failure is returned.
err_h sys_io_prepare(sys_io_pin_t p, sys_io_mode_e mode) {
  err_h err = SYS_IO_WRITE(p, OP_MODE, .mode = (uint8_t)mode);
  (void)err_drop(&err, ERR_DEV_UNKNOWN_OP);
  return err;
}

err_h sys_io_prepare_input(sys_io_pin_t p) {
  err_h err = SYS_IO_WRITE(p, OP_MODE, .mode = SYS_IO_MODE_INPUT_PULLUP);
  if (err_drop(&err, ERR_IO_MODE_UNSUPPORTED)) {  // an expander without pull-ups: the board has an external one
    err = SYS_IO_WRITE(p, OP_MODE, .mode = SYS_IO_MODE_INPUT);
  }
  return err;
}

err_h sys_io_release(sys_io_pin_t p) {
  err_h err = SYS_IO_WRITE(p, OP_RESET);
  (void)err_drop(&err, ERR_DEV_UNKNOWN_OP);
  return err;
}

err_h sys_io_get_level(sys_io_pin_t p, bool* level) {
  ERR_CHECK(level != NULL, ERR_BAD_ARG);
  io_level_args_t answer = {0};
  ERR_TRY(SYS_IO_READ(p, OP_LEVEL, &answer));
  *level = answer.level != 0;
  return NULL;
}

err_h sys_io_get_voltage(sys_io_pin_t p, int32_t* out_mV) {
  ERR_CHECK(out_mV != NULL, ERR_BAD_ARG);
  io_voltage_args_t answer = {0};
  ERR_TRY(SYS_IO_READ(p, OP_VOLTAGE, &answer));
  *out_mV = answer.voltage_mV;
  return NULL;
}
