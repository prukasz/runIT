#pragma once

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The board's own streams, all OUT: LOGS, ERRORS (urgent), TELEMETRY, EVENTS (urgent), then marked the board's
 * (REMOVE refuses them). Part of the streams boot step, after sys_stream_init().
 */
MUST_USE err_h runit_board_streams_init(void);

/**
 * @brief The board's stream routing: the log lines on the BLE LOGS characteristic; every other stream to the clients
 * that subscribe to it. Boot step, after the UART and BLE providers are registered.
 */
MUST_USE err_h runit_board_stream_bindings_init(void);

/**
 * @brief Creates the board's devices (CONFIG_RUNIT_BOARD). A devkit: the ESP GPIO only. The PCB: the ESP GPIO and the
 * TCA6424A (the base: a failure stops here), the status LED (TCA pin 23) lit, then the other chips in dependency order
 * (a failing one is logged, the rest are still created).
 * Boot step, after the I2C buses. IDs and pins: runit_board_defs.h, HARDWARE.md section 3.
 * @return The first failure (owned, not reported yet), or NULL.
 */
MUST_USE err_h runit_board_devices_init(void);

/** @brief The power manager with the board's layout (CONFIG_RUNIT_BOARD: the PCB's rails, monitor and sink; a devkit has
 * none). SYS_POWER_MANAGER.MD. Boot step, after settings and events. */
MUST_USE err_h runit_board_power_init(void);

#ifdef __cplusplus
}
#endif
