#pragma once
#include "status.h"
#include "sys_io.h"

/**
 * @brief Initialize and register the TPS55289 Voltage Regulator device.
 *
 * @param sys_dev_id Unique ID for this device in the sys_device manager
 * @param i2c_address I2C address of the TPS55289 (typically 0x74 or 0x75)
 * @param i2c_bus_num I2C bus number (0 or 1)
 * @param intr_gpio_device_id System device ID of the GPIO controlling
 * interrupt/fault (0xFF if none)
 * @param intr_pin_num Pin number of the interrupt/fault pin
 * @param intr_gpio_mode GPIO mode to initialize interrupt pin into
 * @param en_gpio_device_id System device ID of the GPIO controlling hardware
 * enable (0xFF if none)
 * @param en_pin_num Pin number of the hardware enable pin
 *
 * @return status_rep_t Status of the registration
 */
status_rep_t d_tps55289_create(uint8_t device_id, bool i2c_bus,
                               uint8_t i2c_addr, uint8_t intr_io_device,
                               sys_io_pin_num_t intr_io_num,
                               sys_io_mode_e intr_io_mode, uint8_t en_io_device,
                               sys_io_pin_num_t en_io_num,
                               sys_io_mode_e en_io_mode);

#define DEVICE_TPS55289_MAX_VOLTAGE_MV 20000
#define DEVICE_TPS55289_MIN_VOLTAGE_MV 3000

#define DEVICE_TPS55289_MAX_CURRENT_MA 5500
#define DEVICE_TPS55289_MIN_CURRENT_MA 200
