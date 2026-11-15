#pragma once

#include "errors.h"

/* The module's one number: the module of its errors and the UART contract's ID (sys_uart_contract.h). Claimed in
   sys_uart_contract.c (SYS_DEVICE_CONTRACT_DEFINE(UART, 0x13) and ERR_MODULE_DEFINE). */
MODULE_ID_DECLARE(UART, 0x13)
#define ERR_MOD_SYS_UART MODULE_ID_UART

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_UART)
 *
 * Port send saturation raises ERR_UART_BUSY; when bridged to sys_data_provider, the adapter maps it to ERR_PROV_BUSY.
 * ------------------------------------------------------------------------- */

#define ERR_SYS_UART_MAP(X)                                                                                            \
  X(ERR_UART_BAD_ARG,     ERR_TAG(ERR_MOD_SYS_UART, 1), ERR_SEV_HIGH,   {}) /* NULL pointer, nothing to write, or argument out of contract limits */ \
  X(ERR_UART_BAD_PORT,    ERR_TAG(ERR_MOD_SYS_UART, 2), ERR_SEV_MEDIUM, { uint8_t port; }) /* the link lent, and this board has none */ \
  X(ERR_UART_BUSY,        ERR_TAG(ERR_MOD_SYS_UART, 3), ERR_SEV_LOW,    { uint8_t port; }) /* TX queue full (or SPARE past half) in wait_ms */ \
  X(ERR_UART_BAD_BAUD,    ERR_TAG(ERR_MOD_SYS_UART, 4), ERR_SEV_MEDIUM, { uint32_t baud; }) /* outside SYS_UART_BAUD_MIN..MAX */ \
  X(ERR_UART_INIT,        ERR_TAG(ERR_MOD_SYS_UART, 7), ERR_SEV_HIGH,   { uint8_t port; }) /* the driver refused the port's UART */ \
  X(ERR_UART_NOT_DIRECT,  ERR_TAG(ERR_MOD_SYS_UART, 10), ERR_SEV_MEDIUM, { uint8_t provider; }) /* write or read on what is no DIRECT port */ \
  X(ERR_UART_TIMEOUT,     ERR_TAG(ERR_MOD_SYS_UART, 11), ERR_SEV_MEDIUM, { uint8_t provider; }) /* a write did not leave the line in its time; the link's tasks did not park */ \
  X(ERR_UART_IN_USE,      ERR_TAG(ERR_MOD_SYS_UART, 12), ERR_SEV_MEDIUM, { uint8_t port; uint8_t owner; }) /* the port is held by a reader (subscriber, session, or binding) or unsubscribed by non-owner: `owner` is the device (SYS_DEVICE_ID_NONE: external binding) */

ERR_TAGS_DECLARE(ERR_SYS_UART_MAP)
