#pragma once
#include "status.h"


typedef enum{
    CFG_IO_TYPE_GPIO_MODE = 0,
    CFG_IO_TYPE_GPIO_ADC_ALERT = 1,
    CFG_IO_TYPE_GPIO_INTERRUPT = 2,
    CFG_IO_TYPE_GPIO_PWM_FREQ = 3,
    CFG_IO_TYPE_GPIO_RESET = 4,
    CFG_IO_TYPE_GPIO_SET_LEVEL = 5,
    CFG_IO_TYPE_GPIO_GET_LEVEL = 6,
    CFG_IO_TYPE_GPIO_TOGGLE = 7,
    CFG_IO_TYPE_ADC_READ_MV = 8,
    CFG_IO_TYPE_GPIO_PWM_DUTY = 9,
    CFG_IO_TYPE_RESET_ALL = 10,
}cfg_io_packet_type_e;

typedef enum{
    OUTSIDE_WINDOW = 0,
    INSIDE_WINDOW = 1,
}cfg_io_adc_window_mode_e;

typedef enum{
    INPUT = 0,
    OUTPUT_PUSH_PULL = 1,
    OUTPUT_OPEN_DRAIN = 2,
    INPUT_PULLUP = 3,
    INPUT_PULLDOWN = 4,
    PWM = 5,
    ADC = 6
}cfg_io_gpio_mode_e;

typedef enum{
    RISING_EDGE  = 0,
    FALLING_EDGE = 1,
    BOTH_EDGES   = 2,
    LEVEL_HIGH   = 3,
    LEVEL_LOW    = 4
}cfg_gpio_intr_mode_e;

typedef struct __attribute__((packed)){
    uint32_t pin_id;
    uint32_t mode; //@cfg_io_gpio_mode_e
}cfg_io_gpio_mode_t; //@cfg_io_packet_type_e CFG_IO_TYPE_GPIO_MODE


typedef struct __attribute__((packed)){
    uint32_t pin_id;
    uint32_t cfg_gpio_intr_mode; //@cfg_gpio_intr_mode_e
}cfg_gpio_intr_mode_t; //@cfg_io_packet_type_e CFG_IO_TYPE_GPIO_INTERRUPT

typedef struct __attribute__((packed)){
    uint32_t pin_id;
}cfg_io_gpio_reset_t; //@cfg_io_packet_type_e CFG_IO_TYPE_GPIO_RESET


typedef struct __attribute__((packed)){
    uint32_t pin_id;
    uint32_t adc_threshold_up_mv;
    uint32_t adc_threshold_down_mv;
    uint32_t adc_threshold_hysteresis_mv;
    uint32_t adc_event_counter_threshold;  
    uint32_t adc_window_mode; //@cfg_io_adc_window_mode_e
}cfg_io_gpio_adc_alert_t; //@cfg_io_packet_type_e CFG_IO_TYPE_GPIO_ADC_ALERT

typedef struct __attribute__((packed)){
    uint64_t pin_id;
    uint32_t freq_hz;
}cfg_io_gpio_pwm_freq_t; //@cfg_io_packet_type_e CFG_IO_TYPE_GPIO_PWM_FREQ

typedef struct __attribute__((packed)){
    uint32_t pin_id;
    bool level;
}cfg_io_gpio_set_level_t; //@cfg_io_packet_type_e CFG_IO_TYPE_GPIO_SET_LEVEL

typedef struct __attribute__((packed)){
    uint32_t pin_id;
}cfg_io_gpio_get_level_t; //@cfg_io_packet_type_e CFG_IO_TYPE_GPIO_GET_LEVEL

typedef struct __attribute__((packed)){
    uint32_t pin_id;
}cfg_io_gpio_toggle_t; //@cfg_io_packet_type_e CFG_IO_TYPE_GPIO_TOGGLE

typedef struct __attribute__((packed)){
    uint32_t pin_id;
}cfg_io_adc_read_mv_t; //@cfg_io_packet_type_e CFG_IO_TYPE_ADC_READ_MV

typedef struct __attribute__((packed)){
    uint64_t pin_id;
    uint32_t duty_cycle;
}cfg_io_gpio_pwm_duty_t; //@cfg_io_packet_type_e CFG_IO_TYPE_GPIO_PWM_DUTY

status_rep_t cfg_io_process_packet(const uint8_t* packet_data, uint16_t packet_len);
