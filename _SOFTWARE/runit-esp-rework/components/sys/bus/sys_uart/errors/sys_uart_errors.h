#pragma once

#include "errors.h"

/* The module's one number: the module of its errors and the UART contract's ID (sys_uart_contract.h). Claimed here
   (MODULE_ID_DEFINE in sys_uart.c): the contract's component claims none of its own. */
MODULE_ID_DECLARE(UART, 0x13)
#define ERR_MOD_SYS_UART MODULE_ID_UART

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_UART)
 *
 * A send's failure is the provider's (ERR_PROV_*, SYS_DATA_PROVIDER.MD), ERR_PROV_BUSY raised here.
 * ------------------------------------------------------------------------- */

#define ERR_SYS_UART_MAP(X)                                                                                            \
  X(ERR_UART_BAD_ARG,     ERR_TAG(ERR_MOD_SYS_UART, 1), ERR_SEV_HIGH,   {}) /* NULL pointer, nothing to write */       \
  X(ERR_UART_BAD_PORT,    ERR_TAG(ERR_MOD_SYS_UART, 2), ERR_SEV_MEDIUM, { uint8_t port; }) /* the link lent, and this board has none */ \
  X(ERR_UART_BAD_BAUD,    ERR_TAG(ERR_MOD_SYS_UART, 4), ERR_SEV_MEDIUM, { uint32_t baud; }) /* outside SYS_UART_BAUD_MIN..MAX */ \
  X(ERR_UART_INIT,        ERR_TAG(ERR_MOD_SYS_UART, 7), ERR_SEV_HIGH,   { uint8_t port; }) /* the driver refused the link's UART */ \
  X(ERR_UART_NOT_DIRECT,  ERR_TAG(ERR_MOD_SYS_UART, 10), ERR_SEV_MEDIUM, { uint8_t provider; }) /* write or read on what is no DIRECT port */ \
  X(ERR_UART_TIMEOUT,     ERR_TAG(ERR_MOD_SYS_UART, 11), ERR_SEV_MEDIUM, { uint8_t provider; }) /* a write did not leave the line in its time; the link's tasks did not park */ \
  X(ERR_UART_IN_USE,      ERR_TAG(ERR_MOD_SYS_UART, 12), ERR_SEV_MEDIUM, { uint8_t port; uint8_t owner; }) /* the port has a reader (a subscriber, a session, a binding) or is lent: `owner` is the device (SYS_DEVICE_ID_NONE: no device's) */ \
  X(ERR_UART_NOT_OWNER,   ERR_TAG(ERR_MOD_SYS_UART, 13), ERR_SEV_HIGH,   { uint8_t port; uint8_t owner; }) /* WRITE, READ or LEND off by a caller that did not lend the port: `owner` did */

ERR_TAGS_DECLARE(ERR_SYS_UART_MAP)
