#pragma once
#include "stdint.h"
#include "status.h"

/* Packet type enumeration */
typedef enum{
    CFG_SYS_TYPE_LOG_CONFIG = 0,
    CFG_SYS_TYPE_SYSTEM_CTRL = 1,
    CFG_SYS_TYPE_DEVICE_DEFAULT = 2,
    CFG_SYS_TYPE_VM_RUN = 3, // <----demo 
    CFG_SYS_TYPE_VM_STOP = 4, //<----demo
}cfg_sys_packet_type_e;


typedef enum {
    CFG_ESP_LOG_NONE    = 0,   
    CFG_ESP_LOG_ERROR   = 1,   
    CFG_ESP_LOG_WARN    = 2, 
    CFG_ESP_LOG_INFO    = 3,   
    CFG_ESP_LOG_DEBUG   = 4,    
    CFG_ESP_LOG_VERBOSE = 5,    
    CFG_SP_LOG_MAX      = 6,   
} cfg_log_level_e;


typedef struct __attribute__((packed)){
    bool enable_stream;
    bool mirror_on_serial;
    uint8_t esp_log_level;
} cfg_log_t;


status_rep_t cfg_sys_process_packet(const uint8_t* packet_data, uint16_t packet_len);
