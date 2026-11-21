#pragma once

#include "errors.h"
#include "sys_i2c_id.h"

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_I2C)
 *
 * `bus` is the bus's device ID. A bus device reports its own failures: transfers split by cause, ERR_I2C_NO_ACK (the
 * chip did not answer or the transfer was aborted), ERR_I2C_BUS_BUSY (the bus was not free in time), else
 * ERR_I2C_XFER over its cause (ERR_ESP_ERR on the ESP32-S3); ERR_I2C_ADDR_TAKEN and ERR_I2C_NOT_OWNER name the device
 * that has the address. The client adds ERR_BAD_ARG, ERR_I2C_DEV_NOT_ADDED and, around a register access,
 * ERR_I2C_REG. A missing bus is the registry's ERR_DEV_NOT_FOUND, a bus still in use
 * its ERR_DEV_IN_USE.
 * ------------------------------------------------------------------------- */

#define ERR_SYS_I2C_MAP(X, M)                                                                                                                                                                          \
  X(M, ERR_I2C_BAD_DEV_CFG,   1,  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* not a 7-bit address, or speed 0 */                                                                                \
  X(M, ERR_I2C_BUS_INIT,      2,  ERR_SEV_HIGH,   { uint8_t bus; uint8_t sda; uint8_t scl; }) /* over its cause */                                                                                     \
  X(M, ERR_I2C_BUS_STUCK,     3,  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* probe timed out: bus busy, SCL held low or no pull-ups */                                                         \
  X(M, ERR_I2C_BUS_RESET,     4,  ERR_SEV_HIGH,   { uint8_t bus; }) /* over its cause */                                                                                                               \
  X(M, ERR_I2C_DEV_NOT_FOUND, 5,  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* nothing acked the address */                                                                                      \
  X(M, ERR_I2C_DEV_ADD,       6,  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* over its cause */                                                                                                 \
  X(M, ERR_I2C_DEV_REMOVE,    7,  ERR_SEV_MEDIUM, { uint8_t bus; uint8_t addr; }) /* over its cause */                                                                                                 \
  X(M, ERR_I2C_DEV_NOT_ADDED, 8,  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* transfer to an address not attached */                                                                            \
  X(M, ERR_I2C_NO_ACK,        9,  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* NACK / aborted transfer */                                                                                        \
  X(M, ERR_I2C_BUS_BUSY,      10, ERR_SEV_MEDIUM, { uint8_t bus; uint8_t addr; }) /* bus lock timed out */                                                                                             \
  X(M, ERR_I2C_XFER,          11, ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* other failure, over its cause */                                                                                  \
  X(M, ERR_I2C_REG,           12, ERR_SEV_NONE,   { uint8_t reg; uint8_t write; }) /* context only: register access */                                                                                 \
  X(M, ERR_I2C_ADDR_TAKEN,    13, ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; uint8_t owner; }) /* another chip is attached at this address: `owner` is its device (SYS_DEVICE_ID_NONE: no device) */ \
  X(M, ERR_I2C_NOT_OWNER,     14, ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; uint8_t owner; }) /* DETACH or XFER by a caller that did not attach the address: `owner` did */

ERR_TAGS_DECLARE(MODULE_ID_I2C, ERR_SYS_I2C_MAP)
