#pragma once

#include "errors.h"

#define ERR_SYS_UART_MAP(X)                                                                                         \
  X(ERR_UART_LINE_BAD,     ERR_TAG(ERR_MOD_SYS_UART, 1), ERR_SEV_LOW,    { uint16_t len; }) /* "#R:" line: odd digits, non-hex or too long */ \
  X(ERR_UART_BAD_ENDPOINT, ERR_TAG(ERR_MOD_SYS_UART, 2), ERR_SEV_MEDIUM, { uint32_t endpoint; }) /* connector binding to an endpoint other than 0 */

ERR_TAGS_DECLARE(ERR_SYS_UART_MAP)
