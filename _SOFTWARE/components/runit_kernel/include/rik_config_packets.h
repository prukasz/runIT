#pragma once
#include "stdint.h"
#include <stdbool.h>


/*************************pakiety przeniesione do odpowiedniego managera ****************/
 

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
    bool drv_0_enable;
    bool drv_1_enable;
    bool drv_0_power_source; //A or B
    bool drv_1_power_source; //A or B
    uint32_t drv_0_current_limit_ma;
    uint32_t drv_1_current_limit_ma;
    bool drv_0_ovcp_off_on_fault; //or retry / self recover
    bool drv_1_ovcp_off_on_fault; //or retry / self recover
} rik_cfg_pkt_drv_t;


//Power Delivery Configuration packet structure
typedef struct{
    uint32_t pd_voltage_mv;
    uint32_t pd_current_ma;
} rik_cfg_pkt_pd_t;

