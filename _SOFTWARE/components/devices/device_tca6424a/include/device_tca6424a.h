#pragma once
#include "sys_error.h"
#include "sys_io.h"

/**
 * @brief Configuration for TCA6424A IO Expander device.
 */
typedef struct d_tca6424a_cfg_t {
  uint8_t device_id;
  bool i2c_bus;
  uint8_t i2c_addr;
  sys_io_pin_ref_t intr_pin;
  sys_io_pin_ref_t rst_pin;
} d_tca6424a_cfg_t;

/**
 * @brief Initialize and register the TCA6424A IO Expander device.
 *
 * @param cfg Configuration struct
 * @return err_h Status of the registration
 */
err_h d_tca6424a_create(const d_tca6424a_cfg_t* cfg);
