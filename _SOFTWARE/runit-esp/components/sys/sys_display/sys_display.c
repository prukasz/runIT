#include "sys_display_contract.h"

#include "utils.h"  // the critical section (a spinlock on the board, a mutex in the host tests)

ERR_MODULE_DEFINE(ERR_MOD_SYS_DISPLAY, "sys_display", ERR_SYS_DISPLAY_MAP)

err_h sys_display_get_geometry(uint16_t device, display_geometry_t* out) {
  ERR_CHECK(out != NULL, ERR_DEV_BAD_ARG);
  return sys_device_read(device, DISPLAY_OP_GEOMETRY, NULL, 0, out, sizeof(*out));
}

err_h sys_display_set_backlight(uint16_t device, uint16_t brightness_duty) {
  ERR_CHECK(brightness_duty <= SYS_DISPLAY_BRIGHTNESS_MAX, ERR_DEV_RANGE);
  const display_backlight_args_t a = {.brightness_duty = brightness_duty};
  return sys_device_write(device, DISPLAY_OP_BACKLIGHT, &a, sizeof(a));
}

err_h sys_display_flush(uint16_t device, uint8_t buffer, const display_window_t* window) {
  ERR_CHECK(window != NULL, ERR_DEV_BAD_ARG);
  const display_flush_args_t a = {.buffer_id = buffer, .window = *window};
  return sys_device_write(device, DISPLAY_OP_FLUSH, &a, sizeof(a));
}

/* ---- Buffer table ---- */

typedef struct {
  const void* pixels;  // NULL = not set
  uint32_t size;
} buffer_entry_t;

static buffer_entry_t s_buffers[SYS_DISPLAY_BUFFERS];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

err_h sys_display_buffer_set(uint8_t id, const void* pixels, uint32_t size) {
  ERR_CHECK(id < SYS_DISPLAY_BUFFERS, ERR_DISPLAY_BAD_BUFFER, .buffer = id);
  portENTER_CRITICAL_SAFE(&s_lock);
  s_buffers[id] = (buffer_entry_t){.pixels = pixels, .size = pixels != NULL ? size : 0};
  portEXIT_CRITICAL_SAFE(&s_lock);
  return NULL;
}

err_h sys_display_buffer_get(uint8_t id, const void** out_pixels, uint32_t* out_size) {
  ERR_CHECK(out_pixels != NULL && out_size != NULL, ERR_DEV_BAD_ARG);
  ERR_CHECK(id < SYS_DISPLAY_BUFFERS, ERR_DISPLAY_BAD_BUFFER, .buffer = id);
  buffer_entry_t entry;
  portENTER_CRITICAL_SAFE(&s_lock);
  entry = s_buffers[id];
  portEXIT_CRITICAL_SAFE(&s_lock);
  ERR_CHECK(entry.pixels != NULL, ERR_DISPLAY_BAD_BUFFER, .buffer = id);
  *out_pixels = entry.pixels;
  *out_size = entry.size;
  return NULL;
}
