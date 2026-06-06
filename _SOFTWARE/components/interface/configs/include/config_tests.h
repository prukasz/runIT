#pragma once
#include "status.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CFG_TEST_TYPE_GPIO_SET_LEVEL = 1,
    CFG_TEST_TYPE_GPIO_GET_LEVEL = 2,
    CFG_TEST_TYPE_GPIO_TOGGLE = 3,
    CFG_TEST_TYPE_ADC_READ_MV = 4,
    CFG_TEST_TYPE_GPIO_PWM_DUTY = 5,
    CFG_TEST_TYPE_RESET_ALL = 6,
    CFG_TEST_TYPE_GET_REG_VOLTAGE = 7,
    CFG_TEST_TYPE_GET_REG_CURRENT = 8,
    CFG_TEST_TYPE_GET_SYS_VOLTAGE = 9,
    CFG_TEST_TYPE_GET_SYS_CURRENT = 10
} cfg_test_packet_type_e;

typedef struct __attribute__((packed)){
    uint32_t pin_id;
    bool level;
}cfg_test_gpio_set_level_t; //@cfg_test_packet_type_e CFG_TEST_TYPE_GPIO_SET_LEVEL

typedef struct __attribute__((packed)){
    uint32_t pin_id;
}cfg_test_gpio_get_level_t; //@cfg_test_packet_type_e CFG_TEST_TYPE_GPIO_GET_LEVEL

typedef struct __attribute__((packed)){
    uint32_t pin_id;
}cfg_test_gpio_toggle_t; //@cfg_test_packet_type_e CFG_TEST_TYPE_GPIO_TOGGLE

typedef struct __attribute__((packed)){
    uint32_t pin_id;
}cfg_test_adc_read_mv_t; //@cfg_test_packet_type_e CFG_TEST_TYPE_ADC_READ_MV

typedef struct __attribute__((packed)){
    uint64_t pin_id;
    uint32_t duty_cycle;
}cfg_test_gpio_pwm_duty_t; //@cfg_test_packet_type_e CFG_TEST_TYPE_GPIO_PWM_DUTY

typedef struct __attribute__((packed)){
    uint8_t regulator_num;
}cfg_test_get_reg_voltage_t; //@cfg_test_packet_type_e CFG_TEST_TYPE_GET_REG_VOLTAGE

typedef struct __attribute__((packed)){
    uint8_t regulator_num;
}cfg_test_get_reg_current_t; //@cfg_test_packet_type_e CFG_TEST_TYPE_GET_REG_CURRENT

status_rep_t cfg_tests_process_packet(const uint8_t* packet_data, uint16_t packet_len);