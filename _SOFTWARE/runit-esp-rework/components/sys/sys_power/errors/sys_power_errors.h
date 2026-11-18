#pragma once

#include "errors.h"
#include "sys_power_id.h"

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_SYS_POWER): what a power device reports besides the
 * shared ones in sys_device.h (RANGE, SUSPENDED).
 * ------------------------------------------------------------------------- */

#define ERR_SYS_POWER_MAP(X, M)                                                                                                                                          \
  X(M, ERR_POWER_BAD_CHANNEL,       1, ERR_SEV_HIGH, { uint8_t id; uint8_t channel; }) /* the device has no such channel */                                              \
  X(M, ERR_POWER_ALERT_UNSUPPORTED, 2, ERR_SEV_HIGH, { uint8_t id; uint8_t channel; uint8_t alert; }) /* this channel / device can't do the alert (sys_power_alert_e) */ \
  X(M, ERR_POWER_EXT_FEEDBACK,      3, ERR_SEV_HIGH, { uint8_t id; }) /* the regulator uses an external divider: its output can't be set in mV */                        \
  X(M, ERR_POWER_NO_PROFILE,        4, ERR_SEV_HIGH, { uint8_t id; uint32_t voltage_mV; uint32_t lowest_mV; }) /* the source offers no profile at or below the voltage (lowest_mV: the lowest fixed offer, 5 V when it offers none) */

ERR_TAGS_DECLARE(MODULE_ID_SYS_POWER, ERR_SYS_POWER_MAP)
