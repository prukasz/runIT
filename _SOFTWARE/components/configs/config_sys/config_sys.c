#include "rik_shared.h"
#include "rik_system_ctrl.h"
#include "config_sys.h"
#include "interface_dispatcher.h"
#include "interface_commands.h"

#define TAG __FILE_NAME__

/**
 * Process incoming system configuration packets and apply configurations
 * using appropriate kernel functions
 * 
 * @param packet_data Pointer to packet data (first byte is packet type)
 * @param packet_len Length of packet data
 * @return Status report from configuration application
 */
status_rep_t cfg_sys_process_packet(const uint8_t* packet_data, uint16_t packet_len){
    if (packet_data == NULL || packet_len == 0) {
        ESP_LOGE(TAG, "Invalid packet: null pointer or zero length");
        return STA_ERR;
    }

    switch(packet_data[0]){
        case CFG_SYS_TYPE_LOG_CONFIG: {
            if (packet_len < sizeof(cfg_log_t) + 1) {
                ESP_LOGE(TAG, "Invalid log config packet length");
                return STA_ERR;
            }
            cfg_log_t log_cfg;
            memcpy(&log_cfg, packet_data + 1, sizeof(cfg_log_t));
            
            if (log_cfg.esp_log_level >= CFG_SP_LOG_MAX) {
                ESP_LOGE(TAG, "Invalid log level: %u", log_cfg.esp_log_level);
                return STA_ERR;
            }
            
            ESP_LOGI(TAG, "Applied log config: stream=%d, serial_mirror=%d, log_level=%u",
                     log_cfg.enable_stream, log_cfg.mirror_on_serial, log_cfg.esp_log_level);
            return STA_OK;
        }

        case CFG_SYS_TYPE_SYSTEM_CTRL: {
            if (packet_len < sizeof(cfg_system_ctrl_t) + 1) {
                ESP_LOGE(TAG, "Invalid system control config packet length");
                return STA_ERR;
            }
            cfg_system_ctrl_t sys_ctrl_cfg;
            memcpy(&sys_ctrl_cfg, packet_data + 1, sizeof(cfg_system_ctrl_t));
            
           
            ESP_LOGI(TAG, "Applied system control config: "
                     "REG0[OVP=%u, OCP=%u, SCP=%u] "
                     "REG1[OVP=%u, OCP=%u, SCP=%u] "
                     "WARN[R0=%u, R1=%u, SYS=%u] "
                     "CRIT[R0=%u, R1=%u, SYS=%u]",
                     sys_ctrl_cfg.crt_reg0_ovp,
                     sys_ctrl_cfg.crt_reg0_ocp,
                     sys_ctrl_cfg.crt_reg0_scp,
                     sys_ctrl_cfg.crt_reg1_ovp,
                     sys_ctrl_cfg.crt_reg1_ocp,
                     sys_ctrl_cfg.crt_reg1_scp,
                     sys_ctrl_cfg.crt_current_REG0_WARN,
                     sys_ctrl_cfg.crt_current_REG1_WARN,
                     sys_ctrl_cfg.crt_current_SYS_PWR_WARN,
                     sys_ctrl_cfg.crt_current_REG0_CRIT,
                     sys_ctrl_cfg.crt_current_REG1_CRIT,               
                     sys_ctrl_cfg.crt_current_SYS_PWR_CRIT);
            rik_sys_ctrl_set_cfg((rik_sys_ctrl_cfg_t*)&sys_ctrl_cfg);
            return STA_OK;
        }

        case CFG_SYS_TYPE_DEVICE_DEFAULT: {
            // Apply default device configuration using kernel function
            status_rep_t status = devices_default_config();
            if (status == STA_OK) {
                ESP_LOGI(TAG, "Applied default device configuration");
            } else {
                ESP_LOGE(TAG, "Failed to apply default device configuration: %u", status);
            }
            return status;
        }

        default: {
            ESP_LOGW(TAG, "Unknown system config packet type: 0x%02x", packet_data[0]);
            return STA_ERR;
        }
    }
    
    return STA_OK;
}

/**
 * Initialize the system configuration module by registering 
 * the packet parser with the interface dispatcher
 * 
 * @return Status report from initialization
 */
status_rep_t cfg_sys_init(void){
    status_rep_t status = interface_register_parser(PACKET_H_CFG_SYS, cfg_sys_process_packet);
    if (status == STA_OK) {
        ESP_LOGI(TAG, "System config module initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize system config module");
    }
    return status;
}
