#pragma once
#include "sys_error.h"
#include "sys_io.h"

/**
 * @brief Configuration for TPS55289 Voltage Regulator device.
 */
typedef struct d_tps55289_cfg_t {
  uint8_t device_id;  /**< MUST be first member */
  bool i2c_bus;
  uint8_t i2c_addr;
  sys_io_pin_ref_t intr_pin;
  sys_io_pin_ref_t en_pin;
} d_tps55289_cfg_t;

/**
 * @brief Initialize and register the TPS55289 Voltage Regulator device.
 *
 * @param cfg Pointer to device configuration struct
 * @return err_h Status of registration
 */
err_h d_tps55289_create(const d_tps55289_cfg_t* cfg);


#define DEVICE_TPS55289_MAX_VOLTAGE_MV 20000
#define DEVICE_TPS55289_MIN_VOLTAGE_MV 3000

#define DEVICE_TPS55289_MAX_CURRENT_MA 5500
#define DEVICE_TPS55289_MIN_CURRENT_MA 200
