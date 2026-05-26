#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "manager_io.h"


// 2. ADC-specific storage
typedef struct {
    uint16_t adc_last_read_mv;
    uint16_t adc_cached_mv;
    uint16_t adc_treshold_h_mv;
    uint16_t adc_treshold_l_mv;
    uint16_t hysteresis_mv;
    uint8_t  window_type;
    bool     alert_was_triggered;
    uint8_t  adc_channel; // Keeps track of which ADC channel this pin represents
} pin_adc_data_t;

// 3. PWM-specific storage (Ready for your next module)
typedef struct {
    uint32_t freq_hz;
    uint32_t duty_cycle;
    uint8_t  timer_num;
    uint8_t  channel_num;
} pin_pwm_data_t;

// 4. The Master Unified Pin Object
typedef struct {
    uint32_t io_num;
    sys_gpio_mode_e pin_mode;

    // Callbacks are shared regardless of mode
    void (*callback)(void* arg);
    void* callback_arg;

    // Memory-saving UNION: A pin is only ONE of these at a time.
    union {
        gpio_config_t  gpio_cfg;
        pin_adc_data_t adc_cfg;
        pin_pwm_data_t pwm_cfg;
    } hw; 
} sys_pin_obj_t;

// 5. The Global Registry Array (64 pins max on ESP32)
// Defined in provider_gpio_esp.c, accessible everywhere
extern sys_pin_obj_t* pin_registry[64];