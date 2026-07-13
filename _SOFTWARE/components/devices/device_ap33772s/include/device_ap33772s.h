#pragma once
#include "status.h"
#include "sys_io.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize and register the AP33772S USB PD controller.
 *
 * @param device_id Unique ID for this device in the sys_device manager
 * @param i2c_bus I2C bus number (0 or 1)
 * @param i2c_addr I2C address of the AP33772S
 * @param intr_io_device System device ID of the GPIO controlling interrupt (0xFF if none)
 * @param intr_io_num Pin number of the interrupt pin
 * @param intr_io_mode GPIO mode to initialize interrupt pin into
 *
 * @return status_rep_t Status of the registration
 */
status_rep_t d_ap33772s_create(uint8_t device_id, bool i2c_bus, uint8_t i2c_addr,
                               uint8_t intr_io_device, sys_io_pin_num_t intr_io_num,
                               sys_io_mode_e intr_io_mode);
