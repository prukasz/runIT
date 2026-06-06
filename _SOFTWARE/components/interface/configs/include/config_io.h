#pragma once
#include "status.h"


typedef enum{
    CFG_IO_TYPE_GPIO_MODE = 0,
    CFG_IO_TYPE_GPIO_ADC_ALERT = 1,
    CFG_IO_TYPE_GPIO_INTERRUPT = 2,
    CFG_IO_TYPE_GPIO_PWM_FREQ = 3,
    CFG_IO_TYPE_GPIO_RESET = 4,
}cfg_io_packet_type_e;

typedef enum{
    CFG_IO_ADC_WINDOW_OUTSIDE = 0,
    CFG_IO_ADC_WINDOW_INSIDE = 1,
}cfg_io_adc_window_mode_e;

typedef enum{
    CFG_GPIO_MODE_INPUT = 0,
    CFG_GPIO_MODE_OUTPUT_PUSH_PULL = 1,
    CFG_GPIO_MODE_OUTPUT_OPEN_DRAIN = 2,
    CFG_GPIO_MODE_INPUT_PULLUP = 3,
    CFG_GPIO_MODE_INPUT_PULLDOWN = 4,
    CFG_GPIO_MODE_PWM = 5,
    CFG_GPIO_MODE_ADC = 6
}cfg_io_gpio_mode_e;

typedef enum{
    CFG_GPIO_INTR_MODE_RISING_EDGE  = 0,
    CFG_GPIO_INTR_MODE_FALLING_EDGE = 1,
    CFG_GPIO_INTR_MODE_BOTH_EDGES   = 2,
    CFG_GPIO_INTR_MODE_LEVEL_HIGH   = 3,
    CFG_GPIO_INTR_MODE_LEVEL_LOW    = 4
}cfg_gpio_intr_mode_e;

typedef struct __attribute__((packed)){
    uint32_t pin_id;
    uint32_t mode; 
}cfg_io_gpio_mode_t;


typedef struct __attribute__((packed)){
    uint32_t pin_id;
    uint32_t cfg_gpio_intr_mode;
}cfg_gpio_intr_mode_t;

typedef struct __attribute__((packed)){
    uint32_t pin_id;
}cfg_io_gpio_reset_t;


typedef struct __attribute__((packed)){
    uint32_t pin_id;
    uint32_t adc_threshold_up_mv;
    uint32_t adc_threshold_down_mv;
    uint32_t adc_threshold_hysteresis_mv;
    uint32_t adc_event_counter_threshold;  
    uint32_t adc_window_mode;     
}cfg_io_gpio_adc_alert_t;

typedef struct __attribute__((packed)){
    uint64_t pin_id;
    uint32_t freq_hz;
}cfg_io_gpio_pwm_freq_t;

status_rep_t cfg_io_process_packet(const uint8_t* packet_data, uint16_t packet_len);
