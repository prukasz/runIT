#pragma once

#include "ina3221.h"
#include "status.h"

#define INA_BUS_TPS1 0 
#define INA_BUS_VSUP 1
#define INA_BUS_TPS0 2

#define INA_SHUNT_VALUE 100

#define INA_I2C_ADDR 0x40

/**
 * @brief Configure ina3221 to runIT and bind handle do prepared functions
 */
status_rep_t ina3221_wrapper_init(ina3221_handle_t handle);
/**
 * @brief Get the bus voltage in millivolts. If force_update is true, force read
 * @param bus_num Bus number (0-2)
 * @param voltage_mv Pointer to float to store the voltage in millivolts
 */
status_rep_t io_sys_periph_get_ina3221_bus_voltage(uint8_t bus_num, float* voltage_mv, bool force_update);

/**
 * @brief Sets a critical alert on Channel 0-2 based on a total Power (Watts) target
 * @param channel_num Channel number (0-2)
 */
status_rep_t io_sys_periph_set_ina3221_pwr_crit(uint8_t channel_num, float power_w);

/**
 * @brief Sets a warning alert on Channel 0-2 based on a total Power (Watts) target
 * @param channel_num Channel number (0-2)
 */
status_rep_t io_sys_periph_ina3221_set_pwr_warning(uint8_t channel_num, float power_w);

/**
 * @brief Gets the Shunt Voltage in millivolts
 * @param channel_num Channel number (0-2)
 */

status_rep_t io_sys_periph_ina3221_get_shunt_voltage(uint8_t channel_num, float* voltage_mv, bool force_update);

/**
 * @brief Gets the calculated Current in milliamps
 * @param channel_num Channel number (0-2)
 */
status_rep_t io_sys_periph_ina3221_get_current(uint8_t channel_num, float* current_ma, bool force_update);





