#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wire layouts of the ops (packed, little-endian). contract_i2c.json names them. */

typedef struct __attribute__((packed)) {
  uint8_t flags;     /**< Bit 0 enabled, bit 1 ready, bit 2 pull-ups on */
  uint8_t sda;       /**< SDA pin */
  uint8_t scl;       /**< SCL pin */
  uint32_t used[4];  /**< Bit per 7-bit address: 1 = device added */
} sys_i2c_info_t;

typedef struct __attribute__((packed)) {
  uint32_t found[4];  /**< Bit n = address n acked, 0x08..0x77 */
} sys_i2c_scan_t;

typedef struct __attribute__((packed)) {
  uint8_t bus;  /**< sys_i2c_bus_e */
} sys_i2c_bus_args_t;

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

/**
 * @brief Remote diagnostics of the I2C buses, under ROUTER_KEY_I2C.
 * Scan / reset / raw access only on CONFIG_SYS_I2C_REMOTE_BUS; raw access
 * only at addresses with no device.
 * Ops and layouts: SYS_I2C.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_I2C, sys_i2c_router));
 */
ROUTER_MODULE_DECLARE(sys_i2c_router);

#ifdef __cplusplus
}
#endif
