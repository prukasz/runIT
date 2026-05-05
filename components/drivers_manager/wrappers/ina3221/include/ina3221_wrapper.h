#pragma once

#include "ina3221.h"

#define INA_BUS_TPS1 0 
#define INA_BUS_VSUP 1
#define INA_BUS_TPS0 2

#define INA_SHUNT_VALUE 100

#define INA_I2C_ADDR 0x40


/**
 * @brief Sets a warning alert on Channel 0 and Channel 2 based on a total Power (Watts) target
 */
esp_err_t io_sys_set_max_power_warning(float power_w);

/**
 * @brief Sets a critical alert on Channel 0 based on a total Power (Watts) target
 */
esp_err_t io_sys_set_max_power_critical(float power_w);

/**
 * @brief Gets the Bus Voltage in millivolts
 */
esp_err_t io_sys_get_bus_voltage(uint8_t bus_num, float* voltage_mv, bool force_update);

/**
 * @brief Gets the Shunt Voltage in millivolts
 */
esp_err_t io_sys_get_shunt_voltage(uint8_t channel_num, float* voltage_mv, bool force_update);

/**
 * @brief Gets the calculated Current in milliamps
 */
esp_err_t io_sys_get_current(uint8_t channel_num, float* current_ma, bool force_update);




