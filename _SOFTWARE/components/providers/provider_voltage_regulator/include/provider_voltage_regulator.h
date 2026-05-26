#pragma once
#include "status.h"
#include "driver/i2c_master.h"

void* p_vreg_0_new(void);
void* p_vreg_1_new(void);
i2c_device_config_t* p_vreg_get_i2c_dev_config(bool reg_num);
i2c_master_dev_handle_t*p_vreg_get_i2c_dev_handle(bool reg_num);
TaskHandle_t p_vreg_get_task_handle(bool reg_num);

status_rep_t p_vreg_en(bool reg_num, bool state);
status_rep_t p_vreg_set_voltage(bool reg_num, uint32_t voltage_mv);
status_rep_t p_vreg_set_current_limit(bool reg_num, uint32_t current_ma);
status_rep_t p_vreg_register_ovp_callback(bool reg_num, void (*callback)(void*), void* ctx);
status_rep_t p_vreg_register_ocp_callback(bool reg_num, void (*callback)(void*), void* ctx);
status_rep_t p_vreg_register_scp_callback(bool reg_num, void (*callback)(void*), void* ctx);


extern void p_vreg_intr_pin_fault_callback(void *arg);
