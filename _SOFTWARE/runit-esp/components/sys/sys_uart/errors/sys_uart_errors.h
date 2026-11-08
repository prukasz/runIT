#pragma once

#include "errors.h"

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_UART)
 *
 * CREATE's: the port, the rate, the format and the pins are checked once, where the frame comes in. A send's failure
 * is the provider's (ERR_PROV_*, SYS_DATA_PROVIDER.MD).
 * ------------------------------------------------------------------------- */

#define ERR_SYS_UART_MAP(X)                                                                                            \
  X(ERR_UART_BAD_ARG,     ERR_TAG(ERR_MOD_SYS_UART, 1), ERR_SEV_HIGH,   {}) /* NULL pointer */                         \
  X(ERR_UART_BAD_PORT,    ERR_TAG(ERR_MOD_SYS_UART, 2), ERR_SEV_MEDIUM, { uint8_t port; }) /* the console's, not a UART of the chip, or (a read) not created by CREATE */ \
  X(ERR_UART_PORT_EXISTS, ERR_TAG(ERR_MOD_SYS_UART, 3), ERR_SEV_MEDIUM, { uint8_t port; }) /* CREATE of a port made already */ \
  X(ERR_UART_BAD_BAUD,    ERR_TAG(ERR_MOD_SYS_UART, 4), ERR_SEV_MEDIUM, { uint32_t baud; }) /* outside SYS_UART_BAUD_MIN..MAX */ \
  X(ERR_UART_BAD_FORMAT,  ERR_TAG(ERR_MOD_SYS_UART, 5), ERR_SEV_MEDIUM, { uint8_t mode; uint8_t parity; uint8_t stop_bits; }) /* a mode, parity or stop bits that is none */ \
  X(ERR_UART_BAD_PIN,     ERR_TAG(ERR_MOD_SYS_UART, 6), ERR_SEV_MEDIUM, { uint16_t device; uint8_t pin; }) /* no pin, or not a pin of the ESP32-S3's GPIO device; RTS without CTS */ \
  X(ERR_UART_INIT,        ERR_TAG(ERR_MOD_SYS_UART, 7), ERR_SEV_HIGH,   { uint8_t port; }) /* the driver refused (over ERR_ESP_ERR) */ \
  X(ERR_UART_NO_MEM,      ERR_TAG(ERR_MOD_SYS_UART, 8), ERR_SEV_HIGH,   { uint8_t port; }) /* the port's state, rings or tasks */ \
  X(ERR_UART_NOT_RAW,     ERR_TAG(ERR_MOD_SYS_UART, 9), ERR_SEV_MEDIUM, { uint8_t provider; }) /* a raw command on a framed provider */ \
  X(ERR_UART_NOT_DIRECT,  ERR_TAG(ERR_MOD_SYS_UART, 10), ERR_SEV_MEDIUM, { uint8_t provider; }) /* write or read on what is no DIRECT port */ \
  X(ERR_UART_TIMEOUT,     ERR_TAG(ERR_MOD_SYS_UART, 11), ERR_SEV_MEDIUM, { uint8_t provider; }) /* a write did not leave the line in its time */

ERR_TAGS_DECLARE(ERR_SYS_UART_MAP)
