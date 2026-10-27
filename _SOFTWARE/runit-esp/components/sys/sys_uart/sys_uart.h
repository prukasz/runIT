#pragma once

#include "errors.h"
#include "sys_uart_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Console UART (and USB-Serial-JTAG, when it is a console output) as a
 * router link (TRANSPORT_UART). Frames travel as text lines next to the log:
 *
 *   #R:<hex bytes>\n
 *
 * RX: lines starting with "#R:" (hex in either case) become router frames;
 * every other line is ignored. TX: one uppercase line per response frame,
 * written under the stdout lock so a log line can't land inside it.
 */

/** The only connector endpoint of the UART provider (TRANSPORT_UART). */
#define SYS_UART_ENDPOINT 0u  /**< Console endpoint */

/**
 * @brief Installs the console drivers (stdout moves to the UART driver),
 * registers the router link and the connector provider (both TRANSPORT_UART)
 * and starts the RX task. Call once, after router_init() and
 * sys_data_connector_init().
 */
MUST_USE err_h sys_uart_init(void);

#ifdef __cplusplus
}
#endif
