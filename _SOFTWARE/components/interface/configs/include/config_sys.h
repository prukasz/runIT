#pragma once
#include "stdint.h"
#include "status.h"

typedef enum {
    CFG_ESP_LOG_NONE    = 0,   
    CFG_ESP_LOG_ERROR   = 1,   
    CFG_ESP_LOG_WARN    = 2, 
    CFG_ESP_LOG_INFO    = 3,   
    CFG_ESP_LOG_DEBUG   = 4,    
    CFG_ESP_LOG_VERBOSE = 5,    
    CFG_SP_LOG_MAX     = 6,   
} cfg_log_level_e;


typedef struct __attribute__((packed)){
    bool enable_stream;
    bool mirror_on_serial;
    uint8_t esp_log_level;
} cfg_log_t;

typedef enum{
    CFG_SYS_SYS_CTRL_AUTOMATIC = 0x00,  //<- Predefined
    CFG_SYS_SYS_CTRL_SKIP_EVENT = 0x01, //<- Ignore 
    CFG_SYS_SYS_CTRL_VM_CALLBACKS_ONLY = 0x02, //<- Trigger vm (coed etc)
    CFG_SYS_SYS_CTRL_STOP = 0x03,  //<- Stop devices
    CFG_SYS_CTRL_ENTER_EMERGENCY = 0x04, //<- Stop devices + trigger emergency event (reset all devices, stop vm, etc)
    CFG_SYS_CTRL_DISABLE_DEVICE = 0x05, //<- Disable specific device (if possible)
}cfg_sys_ctrl_mode_e;


typedef struct __attribute__((packed)) rik_sys_ctrl_cfg_t {
    uint8_t crt_reg0_ovp;
    uint8_t crt_reg0_ocp;
    uint8_t crt_reg0_scp;
    uint8_t crt_reg1_ovp;
    uint8_t crt_reg1_ocp;
    uint8_t crt_reg1_scp;
    uint8_t crt_current_REG0_WARN;
    uint8_t crt_current_REG0_CRIT;
    uint8_t crt_current_REG1_WARN;
    uint8_t crt_current_REG1_CRIT;
    uint8_t crt_current_SYS_PWR_WARN;
    uint8_t crt_current_SYS_PWR_CRIT;
}cfg_system_ctrl_t;

/* Packet type enumeration */
typedef enum{
    CFG_SYS_TYPE_LOG_CONFIG = 0,
    CFG_SYS_TYPE_SYSTEM_CTRL = 1,
    CFG_SYS_TYPE_DEVICE_DEFAULT = 2,
}cfg_sys_packet_type_e;


status_rep_t cfg_sys_process_packet(const uint8_t* packet_data, uint16_t packet_len);
