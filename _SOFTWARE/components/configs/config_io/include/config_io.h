#pragma once
#include "status.h"

#define CFG_IO_TYPE_GPIO_MODE 1
#define CFG_IO_TYPE_GPIO_ADC_ALERT 2
#define CFG_IO_TYPE_GPIO_PWM_FREQ 3

typedef __packed struct{
    uint64_t pin_id;
    uint32_t mode; 
}cfg_io_gpio_mode_t;

typedef __packed struct{
    uint64_t pin_id;
    struct{
        uint32_t adc_threshold_up_mv;
        uint32_t adc_threshold_down_mv;
        uint32_t adc_threshold_hysteresis_mv;
        uint32_t adc_event_counter_threshold;  
        uint32_t adc_window_mode;     
    }cfg;
}cfg_io_gpio_adc_alert_t;

typedef __packed struct{
    uint64_t pin_id;
    uint32_t freq_hz;
}cfg_io_gpio_pwm_freq_t;

status_rep_t cfg_io_process_packet(const uint8_t* packet_data, uint16_t packet_len);