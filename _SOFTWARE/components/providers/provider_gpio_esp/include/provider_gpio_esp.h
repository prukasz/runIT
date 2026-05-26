#pragma once
#include "status.h"


status_rep_t p_gpio_esp_set_pin_mode(uint64_t pin_mask, uint32_t mode);

status_rep_t p_gpio_esp_set_level(uint64_t pin_mask, bool level);

status_rep_t p_gpio_esp_read_level(uint64_t pin_mask, bool* level);

status_rep_t p_gpio_esp_pin_toggle(uint64_t pin_mask);

status_rep_t p_gpio_esp_register_callback(uint64_t pin_mask, uint32_t mode, void (*callback)(void* arg), void* arg);

status_rep_t provider_gpio_esp_set_pwm_duty(uint64_t pin_mask, uint32_t duty_cycle);

status_rep_t provider_gpio_esp_set_pwm_freq(uint64_t pin_mask, uint32_t freq_hz);

status_rep_t p_gpio_esp_adc_read(uint64_t pin_mask, uint32_t* out_mv, uint8_t max_results_num);

status_rep_t p_gpio_esp_adc_register_callback(uint64_t pin_mask, void* adc_int_config);

void p_gpio_esp_suppress_updates(bool suppress);

esp_err_t p_gpio_esp_init(void);
