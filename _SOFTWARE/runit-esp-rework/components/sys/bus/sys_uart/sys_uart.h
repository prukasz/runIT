#pragma once

#include <stdint.h>

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The board's UART console as a provider (SYS_UART.MD): framed and wired, carrying COBS-encoded frames with
 * CRC16 between 0x00 bytes (`00 cobs([frame][crc16 LE]) 00`) over UART0 and, when connected, USB-Serial-JTAG.
 *
 * Other UART ports (including the board-to-board link) are `CONTRACT_UART` devices (`device_esp32s3_uart`) attached
 * to `sys_data_provider` via `provider_uart`. `DIRECT` mode for `sys_flasher` returns in Stage 6.
 */

/**
 * @brief Installs the console drivers (`stdout` moves to the UART driver), registers `console_provider` in
 * `sys_data_provider`, and starts its static RX/TX tasks. Once, at boot, before the GPIO device is created.
 * @param[in] console_provider  Provider ID for the console.
 * @return ERR_ESP_ERR or provider registry error.
 */
MUST_USE err_h sys_uart_init(uint8_t console_provider);

#ifdef __cplusplus
}
#endif
