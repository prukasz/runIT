#pragma once
#include "status.h"

// Placeholder header for unified ADS provider

#define vref 3500.0f
#define ratio vref/4096.0f

status_rep_t _sys_adc_expander_read(uint64_t pin_mask, uint32_t* out_mv);
status_rep_t _sys_adc_expander_register_callback(uint64_t pin_mask, void* adc_int_config);
void _sys_adc_expander_delay_updates(bool dereffered_mode);

i2c_master_dev_handle_t provider_adc_expander_get_i2c_dev_handle();
TaskHandle_t provider_adc_expander_get_task_handle();
i2c_device_config_t* provider_adc_expander_get_i2c_dev_config();