#pragma once

#include "errors.h"

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

#define ERR_SYS_UART_MAP(X)                                                                                         \
  X(ERR_UART_LINE_BAD,     ERR_TAG(ERR_MOD_SYS_UART, 1), ERR_SEV_LOW,    { uint16_t len; }) /* "#R:" line: odd digits, non-hex or too long */ \
  X(ERR_UART_BAD_ENDPOINT, ERR_TAG(ERR_MOD_SYS_UART, 2), ERR_SEV_MEDIUM, { uint32_t endpoint; }) /* connector binding to an endpoint other than 0 */

ERR_TAGS_DECLARE(ERR_SYS_UART_MAP)

/**
 * @brief Installs the console drivers (stdout moves to the UART driver),
 * registers the router link and the connector provider (both TRANSPORT_UART)
 * and starts the RX task. Call once, after router_init() and
 * sys_connector_init().
 */
MUST_USE err_h sys_uart_init(void);

#ifdef __cplusplus
}
#endif
