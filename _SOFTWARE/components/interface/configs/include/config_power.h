#pragma once 
#include "status.h"


typedef enum{
    CFG_PWR_TYPE_REG_EN = 1,
    CFG_PWR_TYPE_REG_SETTINGS = 2,
    CFG_PWR_TYPE_REG_LIMITS = 3,
    CFG_PWR_TYPE_REG_BEHAVIOR = 4,

    CFG_PWR_TYPE_SUPPLY = 11,
    CFG_PWR_TYPE_CURRENT_BEHAVIOR = 13
}cfg_pwr_packet_type_e;

typedef enum{
    CFG_CTRL_AUTOMATIC         = 0,  //<- Predefined
    CFG_CTRL_SKIP_EVENT        = 1, //<- Ignore 
    CFG_CTRL_VM_CALLBACKS_ONLY = 2, //<- Trigger vm (coed etc)
    CFG_CTRL_STOP              = 3,  //<- Stop devices
    CFG_CTRL_ENTER_EMERGENCY   = 4, //<- Stop devices + trigger emergency event (reset all devices, stop vm, etc)
    CFG_CTRL_DISABLE_DEVICE    = 5, //<- Disable specific device (if possible)
}cfg_pwr_error_behavior_e;

/******************* REGULATORS  *************************/
typedef struct __attribute__((packed)){
    uint8_t en_reg_0;
    uint8_t en_reg_1;
}cfg_pwr_reg_en_t;

typedef struct __attribute__((packed)){
    uint32_t voltage_reg_0_mV;
    uint32_t voltage_reg_1_mV;
    uint32_t current_limit_reg_0_mA;
    uint32_t current_limit_reg_1_mA;
}cfg_pwr_reg_settings_t;

typedef struct __attribute__((packed)){
    uint32_t power_warning_reg_0_mW;
    uint32_t power_critical_reg_0_mW;
    uint32_t power_warning_reg_1_mW;
    uint32_t power_critical_reg_1_mW;
}cfg_pwr_reg_limits_t;

typedef struct __attribute__((packed)){
    uint8_t behavior_reg0_ovp;
    uint8_t behavior_reg0_ocp;
    uint8_t behavior_reg0_scp;
    uint8_t behavior_reg1_ovp;
    uint8_t behavior_reg1_ocp;
    uint8_t behavior_reg1_scp;
}cfg_pwr_reg_behavior_t;
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
}cfg_pwr_supply_t;

/* Configs for current monitor
monitoring regulators and total sys power*/
typedef struct __attribute__((packed)){
    uint8_t behavior_current_REG0_WARN;
    uint8_t behavior_current_REG0_CRIT;
    uint8_t behavior_current_REG1_WARN;
    uint8_t behavior_current_REG1_CRIT;
    uint8_t behavior_current_SYS_PWR_WARN;
    uint8_t behavior_current_SYS_PWR_CRIT;
} cfg_pwr_current_behavior_t;
/******************* SUPPLY SETTINGS  *************************/


status_rep_t cfg_pwr_process_packet(const uint8_t* packet_data, uint16_t packet_len);

