#pragma once

#include "errors.h"
#include "sys_spi_id.h"

/* Errors of sys_spi_contract (MODULE_ID_SPI). `bus` is the bus's device, `cs` the chip select of a chip on it (SYS_SPI_CS_NONE:
 * a chip without a CS line). A bus device reports its own failures: a controller it could not set up is
 * ERR_SPI_BUS_INIT over its cause, a transfer too long for the bus ERR_SPI_TOO_LONG, any other ERR_SPI_XFER over its
 * cause (the platform's error). ERR_SPI_CS_TAKEN and ERR_SPI_NOT_OWNER name the device that has the chip select. The
 * client adds ERR_BAD_ARG and ERR_SPI_DEV_NOT_ADDED. A missing bus is the registry's ERR_DEV_NOT_FOUND, a bus still in
 * use its ERR_DEV_IN_USE. */

#define ERR_SYS_SPI_MAP(X, M)                                                                                                                                                    \
  X(M, ERR_SPI_BAD_DEV_CFG,   1,  ERR_SEV_HIGH,     { uint8_t bus; uint8_t cs; }) /* mode above 3, clock 0 or above the bus's maximum, queue depth 0 */                          \
  X(M, ERR_SPI_DEV_ADD,       2,  ERR_SEV_HIGH,     { uint8_t bus; uint8_t cs; }) /* the bus could not add the chip, over its cause */                                           \
  X(M, ERR_SPI_DEV_REMOVE,    3,  ERR_SEV_MEDIUM,   { uint8_t bus; uint8_t cs; }) /* the bus could not remove the chip, over its cause */                                        \
  X(M, ERR_SPI_DEV_NOT_ADDED, 4,  ERR_SEV_HIGH,     { uint8_t bus; uint8_t cs; }) /* an op on a chip select not attached */                                                      \
  X(M, ERR_SPI_CS_TAKEN,      5,  ERR_SEV_HIGH,     { uint8_t bus; uint8_t cs; uint8_t owner; }) /* another chip is attached there: `owner` is its device */                     \
  X(M, ERR_SPI_NOT_OWNER,     6,  ERR_SEV_HIGH,     { uint8_t bus; uint8_t cs; uint8_t owner; }) /* an op by a caller that did not attach it: `owner` did */                     \
  X(M, ERR_SPI_NO_SLOT,       7,  ERR_SEV_HIGH,     { uint8_t bus; uint8_t cs; uint8_t slots; }) /* all the bus's `slots` for chips are taken */                                 \
  X(M, ERR_SPI_XFER,          8,  ERR_SEV_HIGH,     { uint8_t bus; uint8_t cs; }) /* the transfer failed, over its cause */                                                      \
  X(M, ERR_SPI_TOO_LONG,      9,  ERR_SEV_HIGH,     { uint8_t bus; uint8_t cs; uint32_t len; uint32_t max; }) /* `len` bytes in one transfer, above the bus's `max` */           \
  X(M, ERR_SPI_QUEUE_FULL,    10, ERR_SEV_MEDIUM,   { uint8_t bus; uint8_t cs; uint8_t depth; }) /* QUEUE with `depth` transfers of the chip queued */                           \
  X(M, ERR_SPI_TIMEOUT,       11, ERR_SEV_HIGH,     { uint8_t bus; uint8_t cs; uint16_t timeout_ms; }) /* queued transfers did not end: XFER not run, DETACH goes on */          \
  X(M, ERR_SPI_BUS_INIT,      12, ERR_SEV_HIGH,     { uint8_t bus; uint8_t sclk; uint8_t mosi; uint8_t miso; }) /* the controller could not be set up (pins: GPIO, 0xFF none) */

ERR_TAGS_DECLARE(MODULE_ID_SPI, ERR_SYS_SPI_MAP)
