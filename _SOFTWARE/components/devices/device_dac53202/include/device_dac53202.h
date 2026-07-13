#pragma once
#include "status.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize and register the DAC53202 device.
 *
 * @param device_id Unique ID for this device in the sys_device manager
 * @param i2c_bus I2C bus number (0 or 1)
 * @param i2c_addr I2C address of the DAC53202
 *
 * @return status_rep_t Status of the registration
 */
status_rep_t d_dac53202_create(uint8_t device_id, bool i2c_bus, uint8_t i2c_addr);
