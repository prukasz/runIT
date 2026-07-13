#pragma once
#include "status.h"
#include "sys_io.h"

/**
 * @brief Initialize and register the PCA9685 PWM Expander device.
 *
 * @param device_id Unique ID for this device in the sys_device manager
 * @param i2c_bus I2C bus number (0 or 1)
 * @param i2c_addr I2C address of the PCA9685
 * @param oe_io_device System device ID of the GPIO controlling OE (0xFF if none)
 * @param oe_io_num Pin number of the OE control pin
 * @param oe_io_mode IO Manager mode to configure the OE pin to
 *
 * @return status_rep_t Status of the registration
 */
status_rep_t d_pca9685_create(uint8_t device_id, bool i2c_bus,
                              uint8_t i2c_addr, uint8_t oe_io_device,
                              sys_io_pin_num_t oe_io_num, sys_io_mode_e oe_io_mode);