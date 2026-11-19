#pragma once

#include <stdint.h>

#include "sys_device.h"
#include "sys_event.h"
#include "sys_power_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The power contracts: not a module with state, but ops that mean the same on every device that has them, like
 * the IO contract. Four contracts, each in its header (contracts/):
 *   - PMON   (0x04xx, sys_power_monitor.h): voltage, current and power of a channel, hardware current alerts
 *     (INA3221, AP33772S).
 *   - VREG   (0x05xx, sys_power_vreg.h): an adjustable regulator: enable, output voltage, current limit (TPS55289,
 *     AP33772S).
 *   - PD     (0x07xx, sys_power_usb_pd.h): a USB-PD sink: the charger's offers, the profile asked for (AP33772S).
 *   - SUPPLY (0x08xx, sys_power_supply.h): a power mux, two switched sources into one load (power_mux).
 * A caller uses the ops with SYS_DEVICE_SET / SYS_DEVICE_GET (sys_device.h); a driver answers with CMD_REPLY. Only a
 * call with logic of its own has a function here. This header holds what they share: the errors and the mW helper.
 */

/**
 * @brief Power in mW of a reading (signed: a reversed current gives a negative power).
 * For a device without a power register: POWER = sys_power_mW(voltage, current).
 * @param[in] voltage_mV Voltage in mV
 * @param[in] current_mA Current in mA, signed
 * @return Power in mW
 */
static inline int32_t sys_power_mW(int32_t voltage_mV, int32_t current_mA) {
  return (int32_t)(((int64_t)voltage_mV * current_mA) / 1000);
}

#ifdef __cplusplus
}
#endif
