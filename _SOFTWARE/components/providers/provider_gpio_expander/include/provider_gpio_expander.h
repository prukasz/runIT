#pragma once 
#include "status.h"
#include "driver/i2c_master.h"







void * p_gpio_expander_new(uint8_t i2c_addr);
void p_gpio_expander_suppress_updates(bool dereffered_mode);

status_rep_t p_gpio_expander_set_pins(uint64_t pin_mask, bool state);
status_rep_t p_gpio_expander_set_pin_callback(uint64_t pin_mask, uint32_t mode, void (*callback)(void* arg), void* arg);
status_rep_t p_gpio_expander_configure_pins(uint64_t pin_mask, uint32_t mode_mask);
status_rep_t p_gpio_expander_read_pins(uint64_t* out_mask);
status_rep_t p_gpio_epander_read_pin(uint64_t pin_mask, bool* out_mask);
status_rep_t p_gpio_expander_toggle_pin(uint64_t pin_mask);

i2c_master_dev_handle_t * p_gpio_expander_get_i2c_dev_handle();
TaskHandle_t p_gpio_expander_get_task_handle();
i2c_device_config_t* p_gpio_expander_get_i2c_dev_config();

extern void provider_gpio_expander_int_callback(void* arg);

