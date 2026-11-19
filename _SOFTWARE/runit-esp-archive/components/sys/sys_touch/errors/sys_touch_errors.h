#pragma once

#include "errors.h"

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_TOUCH): what a touch driver reports besides the
 * shared ones in sys_device.h (BAD_ARG, BAD_PIN, SUSPENDED) and the bus's (sys_i2c).
 * ------------------------------------------------------------------------- */

#define ERR_SYS_TOUCH_MAP(X)                                                                                \
  X(ERR_TOUCH_BAD_CHIP, ERR_TAG(ERR_MOD_SYS_TOUCH, 1), ERR_SEV_HIGH, { uint16_t id; uint32_t chip_id; }) /* CREATE read another chip's ID than the driver's (or the config's chip) */

ERR_TAGS_DECLARE(ERR_SYS_TOUCH_MAP)
