#include "manager_power.h"
#include "manager_io.h"
#include "string.h"
#include "manager_io.h"
#include "manager_power.h"
#include "config_power.h"
#include "rik_shared.h"
#include "rik_system_ctrl.h"
#include "interface_dispatcher.h"
#include "interface_commands.h"

#define TAG __FILE_NAME__

#define MAX_CURRENT_MA 10000
#define MIN_VOLTAGE_MV 4500
#define MAX_VOLTAGE_MV 20000

#undef OWNER
#define OWNER OWNER_MANAGER_PWR_PARSE_PACKET

#define CHECK_AND_RETURN(r) do { \
    status_rep_t _r = (r); \
    if (STA_IS_ERR(_r)) { \
        return STA_W(PWE_ERR_PARSE_FAILED, OWNER, _r.e_code); \
    } \
    return _r; \
} while(0)

status_rep_t cfg_pwr_process_packet(const uint8_t* packet_data, uint16_t packet_len){
    status_rep_t r = STA_OK;
    switch(packet_data[0]){
        case CFG_PWR_TYPE_REG_EN: {
            cfg_pwr_reg_en_t settings;
            memcpy(&settings, packet_data+1, sizeof(cfg_pwr_reg_en_t));
            if (settings.en_reg_0 || settings.en_reg_1){            
                r = SYS_GPIO_SET_LEVEL(RIK_IO_PIN_REGA_EN, settings.en_reg_0);
                r = SYS_GPIO_SET_LEVEL(RIK_IO_PIN_REGB_EN, settings.en_reg_1);
            }
            r = sys_pwr_enable_verg(0, settings.en_reg_0);
            r = sys_pwr_enable_verg(1, settings.en_reg_1);
            CHECK_AND_RETURN(r);
        }
        case CFG_PWR_TYPE_REG_SETTINGS:{
            cfg_pwr_reg_settings_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_pwr_reg_settings_t));
            r = sys_pwr_set_verg_voltage(0, settings.voltage_reg_0_mV);
            r = sys_pwr_set_verg_voltage(1, settings.voltage_reg_1_mV);
            r = sys_pwr_set_verg_current_limit(0, settings.current_limit_reg_0_mA);
            r = sys_pwr_set_verg_current_limit(1,  settings.current_limit_reg_1_mA);
            CHECK_AND_RETURN(r);
        }
        case CFG_PWR_TYPE_REG_LIMITS:{
            cfg_pwr_reg_limits_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_pwr_reg_limits_t));
            r = sys_pwr_set_bus_power_warning(RIK_CHANNEL_VREG0, settings.power_warning_reg_0_mW);
            r = sys_pwr_set_bus_power_warning(RIK_CHANNEL_VREG1, settings.power_warning_reg_1_mW);
            r = sys_pwr_set_bus_power_critical(RIK_CHANNEL_VREG0, settings.power_critical_reg_0_mW);
            r = sys_pwr_set_bus_power_critical(RIK_CHANNEL_VREG1, settings.power_critical_reg_1_mW);
            CHECK_AND_RETURN(r);
        }
        case CFG_PWR_TYPE_REG_BEHAVIOR:{
            rik_sys_ctrl_power_cfg_t* prev_cfg = sys_ctrl_get_power_cfg();
            cfg_pwr_reg_behavior_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_pwr_reg_behavior_t));
            prev_cfg->crt_reg0_ocp = settings.behavior_reg0_ocp;
            prev_cfg->crt_reg0_ovp = settings.behavior_reg0_ovp;
            prev_cfg->crt_reg0_scp = settings.behavior_reg0_scp;
            prev_cfg->crt_reg1_ocp = settings.behavior_reg1_ocp;
            prev_cfg->crt_reg1_ovp = settings.behavior_reg1_ovp;
            prev_cfg->crt_reg1_scp = settings.behavior_reg1_scp;
            CHECK_AND_RETURN(r);
        }
        case CFG_PWR_TYPE_SUPPLY:{
            cfg_pwr_supply_t settings;
            memcpy(&settings, packet_data + 1, sizeof(settings));
            r = sys_pwr_set_bus_current_warning(RIK_CHANNEL_TOTAL, settings.input_current_warning_mA);
            r = sys_pwr_set_bus_current_critical(RIK_CHANNEL_TOTAL, settings.input_current_critical_mA);
            /* add better config later when usb controller will work */
            CHECK_AND_RETURN(r);
        }
        case CFG_PWR_TYPE_CURRENT_BEHAVIOR:{
            rik_sys_ctrl_power_cfg_t* prev_cfg = sys_ctrl_get_power_cfg();
            cfg_pwr_current_behavior_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_pwr_current_behavior_t));
            prev_cfg->crt_current_REG0_CRIT = settings.behavior_current_REG0_CRIT;
            prev_cfg->crt_current_REG0_WARN = settings.behavior_current_REG0_WARN;
            prev_cfg->crt_current_REG1_CRIT = settings.behavior_current_REG1_CRIT;
            prev_cfg->crt_current_REG1_WARN = settings.behavior_current_REG1_WARN;
            prev_cfg->crt_current_SYS_PWR_CRIT = settings.behavior_current_SYS_PWR_CRIT;
            prev_cfg->crt_current_SYS_PWR_WARN = settings.behavior_current_SYS_PWR_WARN;
            CHECK_AND_RETURN(r);
        }
        case CFG_PWR_TYPE_TEST_SET_PD:{
            cfg_pwr_test_set_pd_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_pwr_test_set_pd_t));
            r = sys_pwr_set_pd_voltage_current(settings.pd_voltage_mv, settings.pd_current_ma);
            ESP_LOGI(TAG, "Test PD Set: %lumV, %lumA max", (unsigned long)settings.pd_voltage_mv, (unsigned long)settings.pd_current_ma);
            CHECK_AND_RETURN(r);
        }
        case CFG_PWR_TYPE_TEST_GET_PD_VOLTAGE:{
            uint32_t voltage_mv = 0;
            r = sys_pwr_get_pd_voltage(&voltage_mv);
            ESP_LOGI(TAG, "Test PD Get Voltage: %lumV", (unsigned long)voltage_mv);
            CHECK_AND_RETURN(r);
        }
        case CFG_PWR_TYPE_TEST_GET_PD_CURRENT:{
            int32_t current_ma = 0;
            r = sys_pwr_get_pd_current(&current_ma);
            ESP_LOGI(TAG, "Test PD Get Current: %ldmA", (long)current_ma);
            CHECK_AND_RETURN(r);
        }
        default:
        return STA_W(PWE_ERR_PARSE_NOT_FOUND, OWNER_MANAGER_PWR_PARSE_PACKET, packet_data[0]);
    }
    return STA_OK;
}

 