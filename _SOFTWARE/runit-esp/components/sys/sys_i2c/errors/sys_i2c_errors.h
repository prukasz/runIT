#pragma once

#include "errors.h"

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_I2C)
 *
 * Transfer failures are split by cause: ERR_I2C_NO_ACK (the chip did not
 * answer or the transfer was aborted), ERR_I2C_BUS_BUSY (the bus lock was not
 * free in time), else ERR_I2C_XFER over ERR_ESP_ERR. Every transfer error
 * carries bus and address; the register helpers wrap it in ERR_I2C_REG.
 * ------------------------------------------------------------------------- */

#define ERR_SYS_I2C_MAP(X)                                                                                               \
  X(ERR_I2C_BAD_ARG,        ERR_TAG(ERR_MOD_SYS_I2C, 1),  ERR_SEV_HIGH,   {}) /* NULL pointer or zero length */       \
  X(ERR_I2C_BAD_BUS,        ERR_TAG(ERR_MOD_SYS_I2C, 2),  ERR_SEV_HIGH,   { uint8_t bus; }) /* no such bus, or disabled / not initialized */ \
  X(ERR_I2C_BAD_DEV_CFG,    ERR_TAG(ERR_MOD_SYS_I2C, 3),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* not a 7-bit address, or speed 0 */ \
  X(ERR_I2C_BUS_INIT,       ERR_TAG(ERR_MOD_SYS_I2C, 4),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t sda; uint8_t scl; }) /* over ERR_ESP_ERR */ \
  X(ERR_I2C_BUS_STUCK,      ERR_TAG(ERR_MOD_SYS_I2C, 5),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* probe timed out: bus busy, SCL held low or no pull-ups */ \
  X(ERR_I2C_BUS_RESET,      ERR_TAG(ERR_MOD_SYS_I2C, 6),  ERR_SEV_HIGH,   { uint8_t bus; }) /* over ERR_ESP_ERR */    \
  X(ERR_I2C_DEV_NOT_FOUND,  ERR_TAG(ERR_MOD_SYS_I2C, 7),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* nothing acked the address */ \
  X(ERR_I2C_DEV_ADD,        ERR_TAG(ERR_MOD_SYS_I2C, 8),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* over ERR_ESP_ERR */ \
  X(ERR_I2C_DEV_REMOVE,     ERR_TAG(ERR_MOD_SYS_I2C, 9),  ERR_SEV_MEDIUM, { uint8_t bus; uint8_t addr; }) /* over ERR_ESP_ERR */ \
  X(ERR_I2C_DEV_NOT_ADDED,  ERR_TAG(ERR_MOD_SYS_I2C, 10), ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* transfer before sys_i2c_dev_add */ \
  X(ERR_I2C_NO_ACK,         ERR_TAG(ERR_MOD_SYS_I2C, 11), ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* NACK / aborted transfer */ \
  X(ERR_I2C_BUS_BUSY,       ERR_TAG(ERR_MOD_SYS_I2C, 12), ERR_SEV_MEDIUM, { uint8_t bus; uint8_t addr; }) /* bus lock timed out */ \
  X(ERR_I2C_XFER,           ERR_TAG(ERR_MOD_SYS_I2C, 13), ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* other failure, over ERR_ESP_ERR */ \
  X(ERR_I2C_REG,            ERR_TAG(ERR_MOD_SYS_I2C, 14), ERR_SEV_NONE,   { uint8_t reg; uint8_t write; }) /* context only: register access */ \
  X(ERR_I2C_ADDR_TAKEN,     ERR_TAG(ERR_MOD_SYS_I2C, 15), ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* another device is added at this address */ \
  X(ERR_I2C_REMOTE_DENIED,  ERR_TAG(ERR_MOD_SYS_I2C, 16), ERR_SEV_MEDIUM, { uint8_t bus; }) /* remote op on a bus other than CONFIG_SYS_I2C_REMOTE_BUS */

ERR_TAGS_DECLARE(ERR_SYS_I2C_MAP)
