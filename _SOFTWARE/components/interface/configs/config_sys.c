#include "rik_shared.h"
#include "rik_system_ctrl.h"
#include "rik_logs.h"
#include "config_sys.h"

extern TaskHandle_t vm_demo_task;

#define TAG __FILE_NAME__


status_rep_t cfg_sys_process_packet(const uint8_t* packet_data, uint16_t packet_len){
    switch(packet_data[0]){
        case CFG_SYS_TYPE_LOG_CONFIG: {
            cfg_log_t log_cfg;
            memcpy(&log_cfg, packet_data + 1, sizeof(cfg_log_t));
            sys_log_remote_enable(log_cfg.enable_stream);
            sys_log_mirror_on_serial(log_cfg.mirror_on_serial);
            sys_log_set_level(log_cfg.esp_log_level);
            ESP_LOGI(TAG, "Applied log config: enable_stream=%u, mirror_on_serial=%u, esp_log_level=%u",
                     log_cfg.enable_stream, log_cfg.mirror_on_serial, log_cfg.esp_log_level);
            return STA_OK;
        }
        case CFG_SYS_TYPE_DEVICE_DEFAULT: {
            sys_devices_default_config(); 
            return STA_OK;
        }
        case CFG_SYS_TYPE_VM_RUN: {
            ESP_LOGW(TAG, "Received VM_RUN command - starting VM demo");
            vTaskResume(vm_demo_task);
            return STA_OK;
        }
        case CFG_SYS_TYPE_VM_STOP: {
            ESP_LOGW(TAG, "Received VM_STOP command - stopping VM demo");
            return handle_vm_stop();
        }
    }
    
    return STA_OK;
}


