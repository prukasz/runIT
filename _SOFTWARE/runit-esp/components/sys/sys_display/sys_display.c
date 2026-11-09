#include "sys_display_contract.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_DISPLAY, "sys_display", ERR_SYS_DISPLAY_MAP)

err_h sys_display_get_geometry(uint16_t device, display_geometry_t* out) {
  ERR_CHECK(out != NULL, ERR_DEV_BAD_ARG);
  return sys_device_read(device, DISPLAY_OP_GEOMETRY, NULL, 0, out, sizeof(*out));
}

err_h sys_display_set_backlight(uint16_t device, uint16_t brightness_duty) {
  SYS_DEV_CHECK_IN_RANGE(device, brightness_duty, 0, SYS_DISPLAY_BRIGHTNESS_MAX);
  const display_backlight_args_t a = {.brightness_duty = brightness_duty};
  return sys_device_write(device, DISPLAY_OP_BACKLIGHT, &a, sizeof(a));
}

err_h sys_display_get_backlight(uint16_t device, uint16_t* out) {
  ERR_CHECK(out != NULL, ERR_DEV_BAD_ARG);
  display_backlight_args_t a;
  ERR_TRY(sys_device_read(device, DISPLAY_OP_BACKLIGHT, NULL, 0, &a, sizeof(a)));
  *out = a.brightness_duty;
  return NULL;
}

err_h sys_display_get_health(uint16_t device, display_health_t* out) {
  ERR_CHECK(out != NULL, ERR_DEV_BAD_ARG);
  return sys_device_read(device, DISPLAY_OP_HEALTH, NULL, 0, out, sizeof(*out));
}

err_h sys_display_set_orientation(uint16_t device, uint8_t orientation) {
  SYS_DEV_CHECK_IN_RANGE(device, orientation, SYS_DISPLAY_ORIENTATION_0, SYS_DISPLAY_ORIENTATION_270);
  const display_orientation_args_t a = {.orientation = orientation};
  return sys_device_write(device, DISPLAY_OP_ORIENTATION, &a, sizeof(a));
}

err_h sys_display_get_orientation(uint16_t device, uint8_t* out) {
  ERR_CHECK(out != NULL, ERR_DEV_BAD_ARG);
  display_orientation_args_t a;
  ERR_TRY(sys_device_read(device, DISPLAY_OP_ORIENTATION, NULL, 0, &a, sizeof(a)));
  *out = a.orientation;
  return NULL;
}

err_h sys_display_flush(uint16_t device, const void* pixels, uint32_t len, const display_window_t* window,
                        display_flush_done_f done, void* done_ctx) {
  ERR_CHECK(window != NULL && done != NULL, ERR_DEV_BAD_ARG);
  const display_flush_args_t a = {
      .pixels = pixels, .len = len, .window = *window, .done = done, .done_ctx = done_ctx};
  return sys_device_write(device, DISPLAY_OP_FLUSH, &a, sizeof(a));
}
