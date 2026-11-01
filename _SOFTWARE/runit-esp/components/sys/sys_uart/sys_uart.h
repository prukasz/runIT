#pragma once

#include "errors.h"
#include "sdkconfig.h"
#include "sys_data_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The console UART (and USB-Serial-JTAG, when it is a console output) as one provider (SYS_DATA_PROVIDER.MD
 * "UART"): framed, wired, always up. A frame goes on the line COBS-encoded with its CRC16, between 0x00 bytes:
 *
 *   00 cobs([frame][crc16 LE]) 00
 *
 * RX: the bytes up to each 0x00 are one candidate: decoded, its CRC checked, delivered whole to the provider's user;
 * anything else (the ROM's text, a panic) is dropped. TX: the provider's send queues the frame, the TX task writes it.
 */

/** The console's provider: {SYS_DATA_PROVIDER_LINK_UART, the console's port}. */
#define SYS_UART_PROVIDER \
  ((sys_data_provider_id_t){.link = SYS_DATA_PROVIDER_LINK_UART, .channel = CONFIG_ESP_CONSOLE_UART_NUM})

/**
 * @brief Installs the console drivers (stdout moves to the UART driver), adds the console's provider and starts the
 * RX and TX tasks. Call once, before the session attaches it.
 */
MUST_USE err_h sys_uart_init(void);

#ifdef __cplusplus
}
#endif
