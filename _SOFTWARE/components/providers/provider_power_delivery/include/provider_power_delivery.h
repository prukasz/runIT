#pragma once

#include <stdint.h>
#include "status.h"
#include "driver/i2c_master.h"

void* p_power_delivery_new(void);

i2c_device_config_t* p_power_delivery_get_i2c_dev_config(void);

i2c_master_dev_handle_t* p_power_delivery_get_i2c_dev_handle(void);

TaskHandle_t p_power_delivery_get_task_handle(void);

status_rep_t p_power_delivery_begin(void);

status_rep_t p_power_delivery_set_voltage_and_current(uint32_t voltage_mv, uint32_t current_ma);

status_rep_t p_power_delivery_get_voltage(uint32_t *voltage_mv);

status_rep_t p_power_delivery_get_current(int32_t *current_ma);