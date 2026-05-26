#pragma once
#include "status.h"
#include "driver/i2c_master.h"

#define INA_BUS_TPS1 0 
#define INA_BUS_VSUP 1
#define INA_BUS_TPS0 2

#define INA_SHUNT_VALUE 100

#define INA_I2C_ADDR 0x40


void* p_current_monitor_new(uint8_t i2c_addr);

i2c_device_config_t* p_current_monitor_get_i2c_dev_config(void);
i2c_master_dev_handle_t*p_current_monitor_get_i2c_dev_handle(void);
TaskHandle_t p_current_monitor_get_task_handle(void);


status_rep_t p_current_monitor_set_crit(uint8_t channel, int32_t current_mA);
status_rep_t p_current_monitor_set_crit(uint8_t channel, int32_t current_mA);
status_rep_t p_current_monitor_set_warning(uint8_t channel, int32_t current_mA);

status_rep_t p_current_monitor_get_voltage(uint8_t channel, uint32_t* voltage_mv, bool force_update);

status_rep_t p_current_monitor_get_current(uint8_t channel, int32_t* current_ma, bool force_update);

/**
 * @brief Register a warning alert callback for a specific channel
 * @param channel Target channel (0-2)
 * @param callback Function to invoke when warning alert occurs on this channel
 * @param ctx User context passed to callback
 * @return Status code
 */
status_rep_t p_current_monitor_register_warning_callback(uint8_t channel, void (*callback)(void*), void* ctx);

/**
 * @brief Register a critical alert callback for a specific channel
 * @param channel Target channel (0-2)
 * @param callback Function to invoke when critical alert occurs on this channel
 * @param ctx User context passed to callback
 * @return Status code
 */
status_rep_t p_current_monitor_register_critical_callback(uint8_t channel, void (*callback)(void*), void* ctx);

extern void ina3221_isr_callback_critical(void *arg);
extern void ina3221_isr_callback_warning(void *arg);
