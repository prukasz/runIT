#pragma once
/** Shared declarations required by every device-install decoder header. */

#include <stdint.h>
#include "sys_error.h"
#include "sys_io.h"

#undef OWNER
#define OWNER OWNER_DEC_SYS_DEVICE_INSTALL

#define DEC_SYS_DEVICE_INSTALL_TAG "dec_sys_device_install"

/** Reassemble one flattened sys_io_pin_ref_t from its wire fields. */
static inline sys_io_pin_ref_t pin_ref_from_wire(uint8_t device_id, uint8_t pin, uint8_t mode) {
  return (sys_io_pin_ref_t){.device_id = device_id, .pin = pin, .mode = (sys_io_mode_e)mode};
}
