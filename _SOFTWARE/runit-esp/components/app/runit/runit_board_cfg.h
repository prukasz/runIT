#pragma once

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The board's stream routing: logs and errors on BLE LOGS, telemetry and events on BLE TX; errors, telemetry
 * and events on the UART. Boot step, after the UART and BLE providers are registered.
 */
MUST_USE err_h runit_board_connector_bindings_init(void);

/**
 * @brief Creates the onboard devices: the ESP GPIO and the TCA6424A (the base: a failure stops here), the status LED
 * (TCA pin 23) lit, then the other chips in dependency order (a failing one is logged, the rest are still created).
 * Boot step, after the I2C buses. IDs and pins: runit_board_defs.h, HARDWARE.md section 3.
 * @return The first failure (owned, not reported yet), or NULL.
 */
MUST_USE err_h runit_board_devices_init(void);

/** @brief The power manager with this board's layout (SYS_POWER_MANAGER.MD). Boot step, after settings and events. */
MUST_USE err_h runit_board_power_init(void);

#ifdef __cplusplus
}
#endif
