#pragma once 
#include "status.h"
#include "driver/i2c_master.h"


/**
 * @brief Initialize the PCA9685 provider
 */
void* p_pca9685_new(uint8_t i2c_addr);

/**
 * @brief I2C Manager integration getters
 */
i2c_device_config_t* p_pca9685_get_i2c_dev_config(void);
i2c_master_dev_handle_t* p_pca9685_get_i2c_dev_handle(void);
TaskHandle_t             p_pca9685_get_task_handle(void);

/**
 * @brief IO Manager integration functions
 */
void p_pca9685_freeze(bool freeze);

status_rep_t p_pca9685_pwm_set_duty(uint64_t pin_mask, uint32_t duty_cycle);
status_rep_t p_pca9685_pwm_set_freq(uint64_t pin_mask, uint32_t freq_hz);
status_rep_t p_pca9685_set_pins(uint64_t pin_mask, bool state);
status_rep_t p_pca9685_toggle_pins(uint64_t pin_mask);
/**
 * @brief Reset all PWM expander channels to 0 (off)
 * @return Status code
 */
status_rep_t p_pca9685_reset(void);

status_rep_t p_current_monitor_configure(void);