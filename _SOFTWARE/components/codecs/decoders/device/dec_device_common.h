#pragma once
/** Shared declarations required by every device-install decoder header (pin_ref_wire_t, PIN_REFS_BELOW: sys_io.h). */

#include <stddef.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_io.h"

#undef OWNER
#define OWNER OWNER_DEC_SYS_DEVICE_INSTALL

#define DEC_SYS_DEVICE_INSTALL_TAG "dec_sys_device_install"
