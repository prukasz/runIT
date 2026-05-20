#pragma once 
#include "status.h"
#include "driver/i2c_master.h"







void * provider_gpio_expander_new_handle(uint8_t i2c_addr);
void _sys_expander_gpio_delay_updates(bool dereffered_mode);

status_rep_t _sys_io_expander_set_pin(uint64_t pin_mask, bool state);
status_rep_t _sys_expander_gpio_set_callback(uint64_t pin_mask, uint32_t mode, void (*callback)(void* arg), void* arg);
status_rep_t _sys_expander_configure_pins(uint64_t pin_mask, uint32_t mode_mask);
status_rep_t _sys_io_expander_read_pins(uint64_t* out_mask);
status_rep_t _sys_io_expander_read_pin(uint64_t pin_mask, bool* out_mask);
status_rep_t _sys_io_expander_toggle_pin(uint64_t pin_mask);

i2c_master_dev_handle_t * provider_gpio_expander_get_i2c_dev_handle();
TaskHandle_t provider_gpio_expander_get_task_handle();
i2c_device_config_t* provider_gpio_expander_get_i2c_dev_config();

extern void provider_gpio_expander_int_callback(void* arg);

