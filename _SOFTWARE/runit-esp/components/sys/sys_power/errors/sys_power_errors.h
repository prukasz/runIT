#pragma once

#include "errors.h"

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_POWER): what a power device reports besides the
 * shared ones in sys_device.h (RANGE, SUSPENDED).
 * ------------------------------------------------------------------------- */

#define ERR_SYS_POWER_MAP(X)                                                                                                                                                                                 \
  X(ERR_POWER_BAD_CHANNEL,       ERR_TAG(ERR_MOD_SYS_POWER, 1), ERR_SEV_HIGH, { uint16_t id; uint8_t channel; })                 /* the device has no such channel */                                        \
  X(ERR_POWER_ALERT_UNSUPPORTED, ERR_TAG(ERR_MOD_SYS_POWER, 2), ERR_SEV_HIGH, { uint16_t id; uint8_t channel; uint8_t alert; }) /* this channel / device can't do the alert (sys_power_alert_e) */           \
  X(ERR_POWER_EXT_FEEDBACK,      ERR_TAG(ERR_MOD_SYS_POWER, 3), ERR_SEV_HIGH, { uint16_t id; })                                  /* the regulator uses an external divider: its output can't be set in mV */ \
  X(ERR_POWER_NO_PROFILE,        ERR_TAG(ERR_MOD_SYS_POWER, 4), ERR_SEV_HIGH, { uint16_t id; uint32_t voltage_mV; uint32_t lowest_mV; })    /* the source offers no profile at or below the voltage (lowest_mV: the lowest fixed offer, 5 V when it offers none) */

ERR_TAGS_DECLARE(ERR_SYS_POWER_MAP)
