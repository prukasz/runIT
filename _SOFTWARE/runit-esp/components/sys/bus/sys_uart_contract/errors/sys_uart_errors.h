#pragma once

#include "errors.h"
#include "sys_uart_id.h"

/* The module's one number: the module of its errors and the UART contract's ID (sys_uart_contract.h). Its error
   table is registered in sys_uart_contract.c (ERR_MODULE_DEFINE). */

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_UART)
 *
 * Port send saturation raises ERR_UART_BUSY; bridged to sys_data_provider, the frame is dropped.
 * ------------------------------------------------------------------------- */

// codes 2, 5, 6, 8, 9, 10 and 11 are unused: never assign them
#define ERR_SYS_UART_MAP(X, M)                                                                                             \
  X(M, ERR_UART_BAD_ARG,  1,  ERR_SEV_HIGH,   {}) /* NULL pointer, nothing to write, or argument out of contract limits */ \
  X(M, ERR_UART_BUSY,     3,  ERR_SEV_LOW,    { uint8_t port; }) /* TX queue full (or SPARE past half) in wait_ms */       \
  X(M, ERR_UART_BAD_BAUD, 4,  ERR_SEV_MEDIUM, { uint32_t baud; }) /* outside SYS_UART_BAUD_MIN..MAX */                     \
  X(M, ERR_UART_INIT,     7,  ERR_SEV_HIGH,   { uint8_t port; }) /* the driver refused the port's UART */                  \
  X(M, ERR_UART_IN_USE,   12, ERR_SEV_MEDIUM, { uint8_t port; uint8_t owner; }) /* the port is held by a reader or a send that would break its frames: `owner` the subscriber (SYS_DEVICE_ID_NONE: a hook or a read) */

ERR_TAGS_DECLARE(MODULE_ID_UART, ERR_SYS_UART_MAP)
