#pragma once

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The ids of the board's own providers. A provider's id is only a name: these are this board's choice, not the modules'. */
//#ref-defines board_provider @alias Board's providers @prefix RUNIT_BOARD_PROVIDER_
#define RUNIT_BOARD_PROVIDER_UART_CONSOLE 0x01u  //@alias Console @description The console's UART: a client's session
#define RUNIT_BOARD_PROVIDER_UART_LINK    0x02u  //@alias Board link @description The UART to another board (CONFIG_SYS_UART_LINK)
#define RUNIT_BOARD_PROVIDER_BLE_RUNIT    0x03u  //@alias Bluetooth link @description The runIT BLE pair: a client's session
#define RUNIT_BOARD_PROVIDER_BLE_LOGS     0x04u  //@alias Bluetooth logs @description The LOGS characteristic

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

/** The ID of the ESP32-S3's GPIO device: the profile's pin references (`device` 0) name it. */
#define RUNIT_BOARD_GPIO_DEVICE_ID 0u

/**
 * @brief Creates the ESP32-S3's GPIO device (ID RUNIT_BOARD_GPIO_DEVICE_ID, the board's own, no pins set up). It is
 * not part of the board's profile: every build has it. Boot step, before the profile runs.
 */
MUST_USE err_h runit_board_gpio_init(void);

/** @brief The power manager, with no rails until the board's profile gives them (SYS_POWER_MANAGER.MD). Boot step, after
 * settings and events. */
MUST_USE err_h runit_board_power_init(void);

#ifdef __cplusplus
}
#endif
