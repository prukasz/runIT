#pragma once
#include "status.h"
#include "driver/i2c_master.h"
// Placeholder header for unified ADS provider

#define vref 3500.0f
#define ratio vref/4096.0f

status_rep_t p_adc_expander_read_voltage(uint64_t pin_mask, uint32_t* out_mv, uint8_t max_results_num);
status_rep_t p_adc_expander_register_callback(uint8_t pin_mask, void* adc_int_config);
void p_adc_expander_freeze(bool freeze);

void * p_adc_expander_new_handle(uint8_t i2c_addr);

i2c_master_dev_handle_t *p_adc_expander_get_i2c_dev_handle();
TaskHandle_t p_adc_expander_get_task_handle();
i2c_device_config_t* p_adc_expander_get_i2c_dev_config();

/**
 * @brief Reset all ADC expander alert callbacks and settings
 * @return Status code
 */
status_rep_t p_adc_expander_reset_all(void);

void p_adc_expander_set_port_id(uint8_t port_id);