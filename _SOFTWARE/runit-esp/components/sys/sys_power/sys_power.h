#pragma once

#include "sys_device.h"
#include "sys_event.h"  // SYS_EVENT_NOTIFY in the events tables

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The power contracts (SYS_POWER.MD): not a module with state, but
 * opcodes that mean the same on every device that has them, like the IO
 * contract. Four contracts:
 *   - POWER_MONITOR (0x04xx, sys_power_monitor.h): voltage, current and power
 *     of a channel, hardware current alerts (INA3221, AP33772S).
 *   - POWER_VREG    (0x05xx, sys_power_vreg.h): an adjustable regulator:
 *     enable, output voltage, current limit (TPS55289, AP33772S).
 *   - POWER_USB_PD  (0x07xx, sys_power_usb_pd.h): a USB-PD sink: the charger's
 *     offers, the profile asked for (AP33772S).
 *   - POWER_SUPPLY  (0x08xx, sys_power_supply.h): a power mux, two switched
 *     sources into one load (power_mux).
 * This header holds what they share: the errors and the mW helper.
 */

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

/**
 * @brief Power in mW of a reading (signed: a reversed current gives a negative power).
 * For a device without a power register: GET_POWER = sys_power_mW(voltage, current).
 */
static inline int32_t sys_power_mW(int32_t voltage_mV, int32_t current_mA) {
  return (int32_t)(((int64_t)voltage_mV * current_mA) / 1000);
}

#ifdef __cplusplus
}
#endif
