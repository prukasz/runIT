#pragma once

#include "errors.h"

MODULE_ID_DECLARE(I2C, 0x02)  //@alias I2C contract @description I2C bus contract and module ID
#define ERR_MOD_SYS_I2C MODULE_ID_I2C

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_I2C)
 *
 * `bus` is the bus's device ID. A bus device reports its own failures: transfers split by cause, ERR_I2C_NO_ACK (the
 * chip did not answer or the transfer was aborted), ERR_I2C_BUS_BUSY (the bus was not free in time), else
 * ERR_I2C_XFER over its cause (ERR_ESP_ERR on the ESP32-S3); ERR_I2C_ADDR_TAKEN and ERR_I2C_NOT_OWNER name the device
 * that has the address. The client adds ERR_I2C_BAD_ARG, ERR_I2C_DEV_NOT_ADDED and, around a register access,
 * ERR_I2C_REG. A missing bus is the registry's ERR_DEV_NOT_FOUND, a bus still in use
 * its ERR_DEV_IN_USE.
 * ------------------------------------------------------------------------- */

#define ERR_SYS_I2C_MAP(X)                                                                                               \
  X(ERR_I2C_BAD_ARG,        ERR_TAG(ERR_MOD_SYS_I2C, 1),  ERR_SEV_HIGH,   {}) /* NULL pointer, zero length, a mix of buffers XFER can't do */ \
  X(ERR_I2C_BAD_DEV_CFG,    ERR_TAG(ERR_MOD_SYS_I2C, 3),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* not a 7-bit address, or speed 0 */ \
  X(ERR_I2C_BUS_INIT,       ERR_TAG(ERR_MOD_SYS_I2C, 4),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t sda; uint8_t scl; }) /* over its cause */ \
  X(ERR_I2C_BUS_STUCK,      ERR_TAG(ERR_MOD_SYS_I2C, 5),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* probe timed out: bus busy, SCL held low or no pull-ups */ \
  X(ERR_I2C_BUS_RESET,      ERR_TAG(ERR_MOD_SYS_I2C, 6),  ERR_SEV_HIGH,   { uint8_t bus; }) /* over its cause */      \
  X(ERR_I2C_DEV_NOT_FOUND,  ERR_TAG(ERR_MOD_SYS_I2C, 7),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* nothing acked the address */ \
  X(ERR_I2C_DEV_ADD,        ERR_TAG(ERR_MOD_SYS_I2C, 8),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* over its cause */ \
  X(ERR_I2C_DEV_REMOVE,     ERR_TAG(ERR_MOD_SYS_I2C, 9),  ERR_SEV_MEDIUM, { uint8_t bus; uint8_t addr; }) /* over its cause */ \
  X(ERR_I2C_DEV_NOT_ADDED,  ERR_TAG(ERR_MOD_SYS_I2C, 10), ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* transfer to an address not attached */ \
  X(ERR_I2C_NO_ACK,         ERR_TAG(ERR_MOD_SYS_I2C, 11), ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* NACK / aborted transfer */ \
  X(ERR_I2C_BUS_BUSY,       ERR_TAG(ERR_MOD_SYS_I2C, 12), ERR_SEV_MEDIUM, { uint8_t bus; uint8_t addr; }) /* bus lock timed out */ \
  X(ERR_I2C_XFER,           ERR_TAG(ERR_MOD_SYS_I2C, 13), ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* other failure, over its cause */ \
  X(ERR_I2C_REG,            ERR_TAG(ERR_MOD_SYS_I2C, 14), ERR_SEV_NONE,   { uint8_t reg; uint8_t write; }) /* context only: register access */ \
  X(ERR_I2C_ADDR_TAKEN,     ERR_TAG(ERR_MOD_SYS_I2C, 15), ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; uint8_t owner; }) /* another chip is attached at this address: `owner` is its device (SYS_DEVICE_ID_NONE: no device) */ \
  X(ERR_I2C_NOT_OWNER,      ERR_TAG(ERR_MOD_SYS_I2C, 20), ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; uint8_t owner; }) /* DETACH or XFER by a caller that did not attach the address: `owner` did */

ERR_TAGS_DECLARE(ERR_SYS_I2C_MAP)
