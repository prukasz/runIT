#include "sys_touch_contract.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_TOUCH, "sys_touch", ERR_SYS_TOUCH_MAP)

err_h sys_touch_get_point(uint16_t device, touch_point_t* out) {
  ERR_CHECK(out != NULL, ERR_DEV_BAD_ARG);
  return sys_device_read(device, TOUCH_OP_POINT, NULL, 0, out, sizeof(*out));
}

err_h sys_touch_set_calibration(uint16_t device, const touch_calibration_t* cal) {
  ERR_CHECK(cal != NULL && cal->x_min != cal->x_max && cal->y_min != cal->y_max, ERR_DEV_BAD_ARG);
  return sys_device_write(device, TOUCH_OP_CALIBRATION, cal, sizeof(*cal));
}

err_h sys_touch_get_calibration(uint16_t device, touch_calibration_t* out) {
  ERR_CHECK(out != NULL, ERR_DEV_BAD_ARG);
  return sys_device_read(device, TOUCH_OP_CALIBRATION, NULL, 0, out, sizeof(*out));
}
