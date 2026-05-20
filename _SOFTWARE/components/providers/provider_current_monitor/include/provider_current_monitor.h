#pragma once
#include "status.h"

#define INA_BUS_TPS1 0 
#define INA_BUS_VSUP 1
#define INA_BUS_TPS0 2

#define INA_SHUNT_VALUE 100

#define INA_I2C_ADDR 0x40

/**
 * @brief Configure ina3221 to runIT and bind handle do prepared functions
 */
status_rep_t sys_pwr_init_monitor(void* handle);
/**
 * @brief Get the bus voltage in millivolts. If force_update is true, force read
 * @param bus_num Bus number (0-2)
 * @param voltage_mv Pointer to uint32_t to store the voltage in millivolts
 */
status_rep_t sys_pwr_get_voltage(uint8_t bus_num, uint32_t* voltage_mv, bool force_update);


status_rep_t sys_pwr_set_warning_reg_0(uint32_t power_mw, uint32_t expected_voltage_mv);

status_rep_t sys_pwr_set_crit_reg_0(uint32_t power_mw, uint32_t expected_voltage_mv);

// --- REG 1 (TPS1) ---
status_rep_t sys_pwr_set_warning_reg_1(uint32_t power_mw, uint32_t expected_voltage_mv);

status_rep_t sys_pwr_set_crit_reg_1(uint32_t power_mw, uint32_t expected_voltage_mv);

// --- TOTAL (VSUP) ---
status_rep_t sys_pwr_set_warning_total(uint32_t power_mw, uint32_t expected_voltage_mv);

status_rep_t sys_pwr_set_crit_total(uint32_t power_mw, uint32_t expected_voltage_mv);


/**
 * @brief Gets the calculated Current in milliamps
 * @param channel_num Channel number (0-2)
 */
status_rep_t sys_pwr_get_current(uint8_t channel_num, uint32_t* current_ma, bool force_update);


extern void ina3221_isr_callback_critical(void *arg);
extern void ina3221_isr_callback_warning(void *arg);
