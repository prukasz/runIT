#pragma once 
#include "status.h"
              // The specific handle/struct to pass to it
#define MANAGER_PWR_PACKET_TYPE_POWER_CONFIG 0x01
#define MANAGER_PWR_PACKET_TYPE_REGULATOR_CONFIG 0x02
typedef struct {
    uint32_t output_voltage_expected_mv;
    uint32_t current_limit_ma; 
    uint32_t power_limit_mW;
    bool off_on_fault; 
    uint8_t reg_num; // 0 or 1
    bool output_enable;
} m_pwr_cfg_pkt_regulator_t;   

typedef struct {
    uint32_t input_voltage_expected_mv;
    uint32_t input_current_expected_ma;
    uint32_t input_voltage_warning_mv;    
    uint32_t input_voltage_to_negotiate_mv; //todo usb-c pd 
    uint32_t input_current_to_negotiate_ma;  //todo usb-c pd 
    uint32_t input_voltage_critical_mv;         
    uint32_t power_warning_total_mW;          
    uint32_t power_critical_total_mW;
    uint32_t power_warning_reg_0_mW;
    uint32_t power_critical_reg_0_mW;
    uint32_t power_warning_reg_1_mW;
    uint32_t power_critical_reg_1_mW;   
    uint32_t off_on_short_circuit;  // 1 shutdown, 0 self recover
    uint32_t off_on_over_voltage;  // 1 shutdown, 0 self recover
    uint32_t off_on_over_current;  // 1 shutdown, 0 self recover
    struct {   
        uint32_t under_voltage:1;
        uint32_t over_budget_total:1;
        uint32_t over_budget_reg_0:1;
        uint32_t over_budget_reg_1:1;
        uint32_t _reserved:28;
    } behavior_on_warning;
    struct {   
        uint32_t under_voltage:1;
        uint32_t over_budget_total:1;
        uint32_t over_budget_reg_0:1;
        uint32_t over_budget_reg_1:1;
        uint32_t _reserved:28;
    } behavior_on_critical;
} m_pwr_cfg_pkt_limits_t;