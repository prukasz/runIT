#pragma once 
#include "status.h"


typedef enum{
    CFG_PWR_TYPE_REG_EN = 1,
    CFG_PWR_TYPE_REG_SETTINGS = 2,
    CFG_PWR_TYPE_REG_LIMITS = 3,
    CFG_PWR_TYPE_REG_BEHAVIOR = 4,

    CFG_PWR_TYPE_SUPPLY = 11,
    CFG_PWR_TYPE_SUPPLY_LIMITS = 12,
    CFG_PWR_TYPE_SUPPLY_BEHAVIOR = 13
}cfg_pwr_packet_type_e;

typedef struct __attribute__((packed)){
    uint8_t regulator_num; //0 or 1
    uint8_t enable; //1 to enable, 0 to disable
}cfg_pwr_reg_en_t;

typedef struct __attribute__((packed)){
    uint8_t regulator_num; //0 or 1
    uint32_t voltage_mv;
    uint32_t current_limit_ma;
}cfg_pwr_reg_settings_t;


typedef struct __attribute__((packed)){
    uint32_t power_warning_reg_0_mW;
    uint32_t power_critical_reg_0_mW;
    uint32_t power_warning_reg_1_mW;
    uint32_t power_critical_reg_1_mW;
}cfg_pwr_reg_limits_t;

typedef struct __attribute__((packed)){
    uint32_t reg_number:1; //0 or 1
    uint32_t over_budget_warning:1; 
    uint32_t over_budget_critical:1;
    uint32_t off_on_short_circuit:1;
    uint32_t off_on_over_voltage:1; 
    uint32_t off_on_over_current:1;  
    uint32_t _reserved:26;
}cfg_pwr_reg_behavior_t;


typedef struct __attribute__((packed)){
    uint32_t provided_input_voltage_mv;
    uint32_t provided_input_current_ma;
    uint32_t input_voltage_warning_mV;
    uint32_t input_current_warning_ma;
    uint32_t input_voltage_to_negotiate_mv;
    uint32_t input_current_to_negotiate_ma;
}cfg_pwr_supply_t;

typedef struct __attribute__((packed)){
    uint32_t power_warning_total_mW;
    uint32_t power_critical_total_mW;
}cfg_pwr_supply_limits_t;

typedef struct __attribute__((packed)){
    uint32_t over_budget_warning:1;
    uint32_t over_budget_critical:1;
    uint32_t reserved:30;
} cfg_pwr_supply_behavior_t;


status_rep_t cfg_pwr_process_packet(const uint8_t* packet_data, uint16_t packet_len);

