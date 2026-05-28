#include "manager_power.h"
#include "manager_io.h"
#include "string.h"
#include "manager_io.h"
#include "manager_power.h"
#include "config_power.h"
#include "rik_shared.h"
#include "interface_dispatcher.h"
#include "interface_commands.h"

#define TAG __FILE_NAME__

#define IN_RANGE(val, min, max) ((val) >= (min) && (val) <= (max)) 
#define RETURN_INVALID_ARG(val, name, owner) do { \
    ESP_LOGE(TAG, "Invalid argument %s: %lu", #name, (uint64_t)(val)); \
    return STA_W(PWR_ERR_INVALID_PARAM, owner, (val)); \
} while (0)

#define CHECK_PARAMETER_RANGE(val, min, max, name, owner) do { \
    if (!IN_RANGE((val), (min), (max))) { \
        RETURN_INVALID_ARG((val), (name), (owner)); \
    } \
} while (0)


#define MAX_CURRENT_MA 5500
#define MIN_VOLTAGE_MV 4500
#define MAX_VOLTAGE_MV 20000


status_rep_t cfg_pwr_process_packet(const uint8_t* packet_data, uint16_t packet_len){
    switch(packet_data[0]){ // Assuming first byte is packet type
        case CFG_PWR_TYPE_REG_EN: {
            bool reg_en = packet_data[2];
            uint8_t reg_num = packet_data[1];
            uint64_t reg_pin = (reg_num == 0) ? RIK_IO_PIN_REGA_EN : RIK_IO_PIN_REGB_EN;
            STA_RET_ON_ERR(SYS_GPIO_SET_LEVEL(reg_pin , reg_en));
            return sys_pwr_enable_verg(reg_num, reg_en);
        }
        case CFG_PWR_TYPE_REG_SETTINGS:{
            cfg_pwr_reg_settings_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_pwr_reg_settings_t));
            CHECK_PARAMETER_RANGE(settings.voltage_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV, settings.regulator_num, OWNER_MANAGER_PWR_PARSE_PACKET);
            CHECK_PARAMETER_RANGE(settings.current_limit_ma, 0, MAX_CURRENT_MA, settings.regulator_num, OWNER_MANAGER_PWR_PARSE_PACKET);

            STA_RET_ON_ERR(sys_pwr_set_verg_voltage(settings.regulator_num, settings.voltage_mv));
            return sys_pwr_set_verg_current_limit(settings.regulator_num, settings.current_limit_ma);
        }
        case CFG_PWR_TYPE_REG_LIMITS:{
            cfg_pwr_reg_limits_t settings;
            memcpy(&settings, packet_data + 1, sizeof(cfg_pwr_reg_limits_t));
            STA_RET_ON_ERR(sys_pwr_set_bus_power_warning(0, settings.power_warning_reg_0_mW));
            STA_RET_ON_ERR(sys_pwr_set_bus_power_critical(0, settings.power_critical_reg_0_mW));
            STA_RET_ON_ERR(sys_pwr_set_bus_power_warning(1, settings.power_warning_reg_1_mW));
            return sys_pwr_set_bus_power_critical(1, settings.power_critical_reg_1_mW);
        }
        case CFG_PWR_TYPE_REG_BEHAVIOR:{
            ESP_LOGW(TAG, "Received regulator behavior config packet - behavior configuration not implemented yet");
            return STA_OK;
        }
        case CFG_PWR_TYPE_SUPPLY:{
            ESP_LOGW(TAG, "Received supply config packet - supply configuration not implemented yet");
            return STA_OK;
        }
        case CFG_PWR_TYPE_SUPPLY_LIMITS:{
            cfg_pwr_supply_limits_t limits;
            memcpy(&limits, packet_data + 1, sizeof(cfg_pwr_supply_limits_t));
            STA_RET_ON_ERR(sys_pwr_set_bus_power_warning(RIK_CHANNEL_TOTAL, limits.power_warning_total_mW));
            return sys_pwr_set_bus_power_critical(RIK_CHANNEL_TOTAL, limits.power_critical_total_mW);
        }
        case CFG_PWR_TYPE_SUPPLY_BEHAVIOR:{
            ESP_LOGW(TAG, "Received supply behavior config packet - behavior configuration not implemented yet");
            return STA_OK;
        }

        return STA_W(PWE_ERR_PARSE_NOT_FOUND, OWNER_MANAGER_PWR_PARSE_PACKET, packet_data[0]);
    }
    return STA_OK;
}

