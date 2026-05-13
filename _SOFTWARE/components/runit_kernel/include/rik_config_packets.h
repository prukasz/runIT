#pragma once
#include "stdint.h"

//Logs and status report configuration packet structure
typedef struct{
    bool enable_stream;
    bool mirror_on_serial;
    uint8_t esp_log_level; // (0-5, corresponding to ESP_LOG_NONE to ESP_LOG_VERBOSE)
    struct{
        uint8_t log_i:1;
        uint8_t log_w:1;
        uint8_t log_c:1;
        uint8_t rep_i:1;
        uint8_t rep_w:1;
        uint8_t rep_c:1;
        uint8_t _reserved:2;
    }status_log_cfg;   //config of status_rep_t logging, independent form esp_log
}rik_cfg_pkt_log_t;


//TPS55289 Configuration packet structure
//other params are hidden from user
typedef struct{
    struct{
        uint8_t i2c_address;
        uint8_t bus_num;
        uint8_t opt_dev_id;  //from device register
    }dev_identifier;
    bool  off_on_fault; 
    uint32_t in_voltage_expected_mv;
    uint32_t current_limit_ma; 
 //or retry on fault
}rik_cfg_pkt_tps55289_t;   



//Total board power configuration packet structure 
/**
 * uint32 może być bo to jest konfiguracja i nie trzeba oszczędzać miejsca w pakiecie
 * tu są chyba wszystkie możliwe ustawienia dla użytkownika 
 * trzeba dobrać teraz parametry
 */
typedef struct {
    /* --- Voltage & Current (32-bit integers) --- */
    uint32_t in_voltage_expected_mv;
    uint32_t in_voltage_warning_mv;    
    uint32_t in_voltage_to_negotiate_mv; 
    uint32_t in_current_to_negotiate_ma; 
    uint32_t in_voltage_critical_mv;   
    uint32_t power_budget_ma;           

    /* --- Power (32-bit integers) --- */
    uint32_t power_warning_in_mW;          
    uint32_t power_critical_in_mW;
    uint32_t power_warning_tps_0_mW;
    uint32_t power_critical_tps_0_mW;
    uint32_t power_warning_tps_1_mW;
    uint32_t power_critical_tps_1_mW;   

    uint32_t on_scp;  // 1 shutdown, 0 self recover
    uint32_t on_ovp;  // 1 shutdown, 0 self recover

    struct {   
        uint32_t under_voltage:1;
        uint32_t over_budget_total:1;
        uint32_t over_budget_tps_0:1;
        uint32_t over_budget_tps_1:1;
        uint32_t _reserved:28;
    } behavior_on_warning;
    struct {   
        uint32_t under_voltage:1;
        uint32_t over_budget_total:1;
        uint32_t over_budget_tps_0:1;
        uint32_t over_budget_tps_1:1;
        uint32_t _reserved:28;
    } behavior_on_critical;
} rik_cfg_pkt_power_t;

typedef struct{
    bool drv_0_enable;
    bool drv_1_enable;
    bool drv_0_power_source; //A or B
    bool drv_1_power_source; //A or B
    uint32_t drv_0_current_limit_ma;
    uint32_t drv_1_current_limit_ma;
    bool drv_0_ovcp_off_on_fault; //or retry / self recover
    bool drv_1_ovcp_off_on_fault; //or retry / self recover
} rik_cfg_pkt_drv_t;


//ADS7128 Configuration packet structure