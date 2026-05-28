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
    switch(packet_data[0]){
        case CFG_SYS_TYPE_LOG_CONFIG: {
            cfg_log_t log_cfg;
            memcpy(&log_cfg, packet_data + 1, sizeof(cfg_log_t));
            break;
        }
        case CFG_SYS_TYPE_SYSTEM_CTRL: {
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

        break;        
        }
    }
    
    return STA_OK;
}


