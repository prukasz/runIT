#pragma once
#include "sys_error.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Configuration for DAC53202 device.
 */
typedef struct d_dac53202_cfg_t {
  uint8_t device_id; /**< MUST be first member */
  bool i2c_bus;
  uint8_t i2c_addr;
} d_dac53202_cfg_t;

/**
 * @brief Initialize and register the DAC53202 device.
 *
 * @param cfg Pointer to device configuration struct
 * @return err_h Status of the registration
 */
err_h d_dac53202_create(const d_dac53202_cfg_t* cfg);

