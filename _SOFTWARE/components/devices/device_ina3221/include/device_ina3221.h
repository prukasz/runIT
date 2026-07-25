#pragma once
#include "sys_error.h"
#include "sys_io.h"

/**
 * @brief Configuration for INA3221 Current/Voltage Monitor device.
 */
typedef struct d_ina3221_cfg_t {
  uint8_t device_id;
  bool i2c_bus;
  uint8_t i2c_addr;
  sys_io_pin_ref_t crit_pin;
  sys_io_pin_ref_t warn_pin;
} d_ina3221_cfg_t;

/**
 * @brief Initialize and register the INA3221 Current/Voltage Monitor device.
 *
 * @param cfg Configuration struct
 * @return err_h Status of the registration
 */
err_h d_ina3221_create(const d_ina3221_cfg_t* cfg);
