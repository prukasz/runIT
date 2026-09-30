#pragma once
/** Shared declarations required by every device-install decoder header. */

#include <stddef.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_io.h"

#undef OWNER
#define OWNER OWNER_DEC_SYS_DEVICE_INSTALL

#define DEC_SYS_DEVICE_INSTALL_TAG "dec_sys_device_install"

/**
 * A pin on another device, as install packets carry it: the provider's device ID,
 * the pin (SYS_GPIO_NONE = not connected) and its sys_io_mode_e. The device
 * generator expands a field of this type to <name>_device_id / _pin / _mode and one
 * pin group; annotate the field with @alias, @note, @modes [...], @default-mode.
 */
typedef struct __packed {
  uint8_t device_id;
  uint8_t pin;
  uint8_t mode;
} pin_ref_wire_t;
_Static_assert(sizeof(pin_ref_wire_t) == 3, "pin_ref_wire_t is three bytes on the wire");

static inline sys_io_pin_ref_t pin_ref_from_wire(pin_ref_wire_t wire) {
  return (sys_io_pin_ref_t){.device_id = wire.device_id, .pin = wire.pin, .mode = (sys_io_mode_e)wire.mode};
}

/**
 * A device's pins must be on a lower-ID device (SYS_DEVICE.MD, sweep
 * direction): the board resumes devices from the lowest ID and suspends /
 * removes them from the highest, so a dependent goes down before the device
 * its pins are on. Unused pins (SYS_GPIO_NONE) pass.
 */
static inline SE_MUST_USE err_h pin_refs_below(uint8_t device_id, const sys_io_pin_ref_t* refs, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (sys_io_pin_is_valid(refs[i]) && refs[i].device_id >= device_id) {
      SE_FAIL(ERR_DEV_PIN_ORDER, .dev_id = device_id, .pin_dev_id = refs[i].device_id, .pin = refs[i].pin);
    }
  }
  return NULL;
}
#define PIN_REFS_BELOW(device_id, ...) \
  pin_refs_below((device_id), (const sys_io_pin_ref_t[]){__VA_ARGS__}, sizeof((sys_io_pin_ref_t[]){__VA_ARGS__}) / sizeof(sys_io_pin_ref_t))
