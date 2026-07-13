#pragma once
#include "status.h"
#include "sys_io.h"

#define INA_BUS_TPS1 0 
#define INA_BUS_VSUP 1
#define INA_BUS_TPS0 2

#define INA_SHUNT_VALUE 100
#define INA_I2C_ADDR 0x40

/**
 * @brief Initialize and register the INA3221 Current/Voltage Monitor device.
 * 
 * @param device_id Unique ID for this device in the sys_device manager
 * @param i2c_bus I2C bus number (0 or 1)
 * @param i2c_addr I2C address of the INA3221 (typically 0x40 to 0x43)
 * @param crit_io_device System device ID of the GPIO controlling Critical Alert (0xFF if none)
 * @param crit_io_num Pin number of the Critical Alert pin
 * @param crit_io_mode GPIO mode to initialize Critical Alert pin into
 * @param warn_io_device System device ID of the GPIO controlling Warning Alert (0xFF if none)
 * @param warn_io_num Pin number of the Warning Alert pin
 * @param warn_io_mode GPIO mode to initialize Warning Alert pin into
 * 
 * @return status_rep_t Status of the registration
 */
status_rep_t d_ina3221_create(
    uint8_t device_id,
    bool i2c_bus,
    uint8_t i2c_addr,
    uint8_t crit_io_device,
    sys_io_pin_num_t crit_io_num,
    sys_io_mode_e crit_io_mode,
    uint8_t warn_io_device,
    sys_io_pin_num_t warn_io_num,
    sys_io_mode_e warn_io_mode
);
