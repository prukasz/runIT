#pragma once

#include "router.h"
#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_I2C (SYS_I2C.MD "Remote"), each above the frame it uses (packed, little-endian; contract_i2c.json
 * names them), with its sides. Never renumber; 0x01 is unused: never assign it. */

#define SYS_I2C_OP_RESET      0x03u  // W: sys_i2c_bus_args_t
#define SYS_I2C_OP_BUS_REMOVE 0x06u  // W: sys_i2c_bus_args_t
typedef struct __attribute__((packed)) {
  uint8_t bus;  /**< sys_i2c_bus_e */
} sys_i2c_bus_args_t;

#define SYS_I2C_OP_SCAN 0x02u  // R: sys_i2c_bus_args_t -> sys_i2c_scan_t
typedef struct __attribute__((packed)) {
  uint32_t found[4];  /**< Bit n = address n acked, 0x08..0x77 */
} sys_i2c_scan_t;

#define SYS_I2C_OP_RAW 0x04u  // R: sys_i2c_raw_read_args_t -> sys_i2c_raw_write_args_t + data; W: sys_i2c_raw_write_args_t + data
typedef struct __attribute__((packed)) {
  uint8_t bus;   /**< sys_i2c_bus_e */
  uint8_t addr;  /**< 7-bit address */
  uint8_t reg;   /**< Register address */
  uint8_t len;   /**< Length in bytes */
} sys_i2c_raw_read_args_t;

typedef struct __attribute__((packed)) {
  uint8_t bus;   /**< sys_i2c_bus_e */
  uint8_t addr;  /**< 7-bit address */
  uint8_t reg;   /**< Register address; the data follows (at least 1 byte) */
} sys_i2c_raw_write_args_t;

#define SYS_I2C_OP_BUS_CREATE 0x05u  // R: sys_i2c_bus_args_t -> sys_i2c_bus_cfg_t; W: sys_i2c_bus_cfg_t
typedef struct __attribute__((packed)) sys_i2c_bus_cfg {
  uint8_t bus;       /**< sys_i2c_bus_e */
  sys_io_pin_t sda;  /**< A pin of the ESP32-S3's GPIO device */
  sys_io_pin_t scl;  /**< A pin of the ESP32-S3's GPIO device */
  uint8_t pullup;    /**< 1: the ESP32-S3's internal pull-ups on */
} sys_i2c_bus_cfg_t;

/**
 * @brief The I2C buses and their diagnostics, under ROUTER_KEY_I2C.
 * Raw access only at addresses with no device.
 * Ops and layouts: SYS_I2C.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_I2C, sys_i2c_router));
 */
ROUTER_MODULE_DECLARE(sys_i2c_router);

#ifdef __cplusplus
}
#endif
