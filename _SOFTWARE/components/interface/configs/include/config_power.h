#pragma once 
#include "status.h"


typedef enum{
    CFG_PWR_TYPE_REG_EN = 1,
    CFG_PWR_TYPE_REG_SETTINGS = 2,
    CFG_PWR_TYPE_REG_LIMITS = 3,
    CFG_PWR_TYPE_REG_BEHAVIOR = 4,
    CFG_PWR_TYPE_SUPPLY = 11,
    CFG_PWR_TYPE_CURRENT_BEHAVIOR = 13,
    CFG_PWR_TYPE_TEST_SET_PD = 14,
    CFG_PWR_TYPE_TEST_GET_PD_VOLTAGE = 15,
    CFG_PWR_TYPE_TEST_GET_PD_CURRENT = 16
}cfg_pwr_packet_type_e;

typedef enum{
    AUTOMATIC         = 0,  //<- Predefined
    IGNORE        = 1, //<- Ignore 
    VM_CALLBACKS_ONLY = 2, //<- Trigger vm (coed etc)
    STOP              = 3,  //<- Stop devices
    EMERGENCY   = 4, //<- Stop devices + trigger emergency event (reset all devices, stop vm, etc)
    DISABLE_DEVICE    = 5, //<- Disable specific device (if possible)
}cfg_pwr_error_behavior_e;

/******************* REGULATORS  *************************/
typedef struct __attribute__((packed)){
    bool en_reg_0;
    bool en_reg_1;
}cfg_pwr_reg_en_t; //@cfg_pwr_packet_type_e CFG_PWR_TYPE_REG_EN

typedef struct __attribute__((packed)){
    uint32_t voltage_reg_0_mV;
    uint32_t voltage_reg_1_mV;
    uint32_t current_limit_reg_0_mA;
    uint32_t current_limit_reg_1_mA;
}cfg_pwr_reg_settings_t; //@cfg_pwr_packet_type_e CFG_PWR_TYPE_REG_SETTINGS

typedef struct __attribute__((packed)){
    uint32_t power_warning_reg_0_mW;
    uint32_t power_critical_reg_0_mW;
    uint32_t power_warning_reg_1_mW;
    uint32_t power_critical_reg_1_mW;
}cfg_pwr_reg_limits_t; //@cfg_pwr_packet_type_e CFG_PWR_TYPE_REG_LIMITS

typedef struct __attribute__((packed)){
    uint8_t behavior_reg0_ovp; //@cfg_pwr_error_behavior_e
    uint8_t behavior_reg0_ocp; //@cfg_pwr_error_behavior_e
    uint8_t behavior_reg0_scp; //@cfg_pwr_error_behavior_e
    uint8_t behavior_reg1_ovp; //@cfg_pwr_error_behavior_e
    uint8_t behavior_reg1_ocp; //@cfg_pwr_error_behavior_e
    uint8_t behavior_reg1_scp; //@cfg_pwr_error_behavior_e
}cfg_pwr_reg_behavior_t; //@cfg_pwr_packet_type_e CFG_PWR_TYPE_REG_BEHAVIOR
/******************* REGULATORS  *************************/

/******************* SUPPLY SETTINGS  *************************/
typedef struct __attribute__((packed)){
    uint32_t provided_input_voltage_mv;
    uint32_t provided_input_current_ma;
    uint32_t input_voltage_warning_mV;
    uint32_t input_voltage_critical_mV;
    int32_t input_current_warning_mA;
    int32_t input_current_critical_mA;
    uint32_t input_voltage_to_negotiate_mv;
    uint32_t input_current_to_negotiate_ma;
}cfg_pwr_supply_t; //@cfg_pwr_packet_type_e CFG_PWR_TYPE_SUPPLY

/* Configs for current monitor
monitoring regulators and total sys power*/
typedef struct __attribute__((packed)){
    uint8_t behavior_current_REG0_WARN; //@cfg_pwr_error_behavior_e
    uint8_t behavior_current_REG0_CRIT; //@cfg_pwr_error_behavior_e
    uint8_t behavior_current_REG1_WARN; //@cfg_pwr_error_behavior_e
    uint8_t behavior_current_REG1_CRIT; //@cfg_pwr_error_behavior_e
    uint8_t behavior_current_SYS_PWR_WARN; //@cfg_pwr_error_behavior_e
    uint8_t behavior_current_SYS_PWR_CRIT; //@cfg_pwr_error_behavior_e
} cfg_pwr_current_behavior_t; //@cfg_pwr_packet_type_e CFG_PWR_TYPE_CURRENT_BEHAVIOR
/******************* SUPPLY SETTINGS  *************************/

/******************* PD TESTS *************************/
typedef struct __attribute__((packed)){
    uint32_t pd_voltage_mv;
    uint32_t pd_current_ma;
} cfg_pwr_test_set_pd_t; //@cfg_pwr_packet_type_e CFG_PWR_TYPE_TEST_SET_PD


status_rep_t cfg_pwr_process_packet(const uint8_t* packet_data, uint16_t packet_len);
