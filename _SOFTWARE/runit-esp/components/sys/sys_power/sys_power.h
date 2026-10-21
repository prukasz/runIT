#pragma once

#include "sys_device.h"
#include "sys_event.h"  // SYS_EVENT_NOTIFY in the events tables
#include "sys_power_errors.h"

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
