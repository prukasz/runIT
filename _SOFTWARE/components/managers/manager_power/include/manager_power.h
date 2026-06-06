#pragma once 
#include "status.h"


typedef enum manager_pwr_cb_type_e{
    MANAGER_PWR_CB_REG0_OVP,
    MANAGER_PWR_CB_REG0_OCP,
    MANAGER_PWR_CB_REG0_SCP,

    MANAGER_PWR_CB_REG1_OVP,
    MANAGER_PWR_CB_REG1_OCP,
    MANAGER_PWR_CB_REG1_SCP,

    MANAGER_PWR_CB_CURRENT_REG0_WARNING,
    MANAGER_PWR_CB_CURRENT_REG0_CRITICAL,
    MANAGER_PWR_CB_CURRENT_REG1_WARNING,
    MANAGER_PWR_CB_CURRENT_REG1_CRITICAL,
    MANAGER_PWR_CB_CURRENT_SYS_WARNING,
    MANAGER_PWR_CB_CURRENT_SYS_CRITICAL
} manager_pwr_cb_type_e;


void manager_pwr_freeze(bool freeze);
status_rep_t manager_pwr_init();
status_rep_t manager_pwr_add_cb(manager_pwr_cb_type_e cb_type, void (*handler)(void *), void* ctx);

status_rep_t sys_pwr_set_bus_current_warning(uint8_t channel, int32_t current_mA);
status_rep_t sys_pwr_set_bus_current_critical(uint8_t channel, int32_t current_mA);
status_rep_t sys_pwr_set_bus_power_warning(uint8_t channel, int32_t power_mW);
status_rep_t sys_pwr_set_bus_power_critical(uint8_t channel, int32_t power_mW);
status_rep_t sys_pwr_get_bus_voltage(uint8_t channel, uint32_t *voltage_mV);
status_rep_t sys_pwr_get_bus_current(uint8_t channel, int32_t *current_mA);
status_rep_t sys_pwr_current_monitor_reset(void);

status_rep_t sys_pwr_set_verg_voltage(bool regulator_id, uint32_t voltage_mv);
status_rep_t sys_pwr_set_verg_current_limit(bool regulator_id, uint32_t current_ma);
status_rep_t sys_pwr_enable_verg(bool regulator_id, bool enable);

 