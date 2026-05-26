#include "manager_power.h"
#include "manager_io.h"
#include "string.h"
#include "manager_io.h"
#include "manager_power.h"
#include "config_power.h"

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


static m_pwr_cfg_pkt_limits_t current_power_config;
static m_pwr_cfg_pkt_regulator_t current_regulator_configs[2]; 

static struct{
    uint32_t reg_1_budget_mW;
    uint32_t reg_0_budget_mW;
    uint32_t current_power_budget_mW;
    uint32_t current_power_budget_use_mW;
}power_budget;

static status_rep_t validate_power_config(const m_pwr_cfg_pkt_limits_t* config) {
    //CHECK_PARAMETER_RANGE(val, min, max, name, owner)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wtype-limits"
    CHECK_PARAMETER_RANGE(config->input_voltage_expected_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV, input_voltage_expected_mv, OWNER_MANAGER_PWR_PARSE_PACKET);
    CHECK_PARAMETER_RANGE(config->input_voltage_warning_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV, input_voltage_warning_mv, OWNER_MANAGER_PWR_PARSE_PACKET);
    CHECK_PARAMETER_RANGE(config->input_voltage_critical_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV, input_voltage_critical_mv, OWNER_MANAGER_PWR_PARSE_PACKET);
    CHECK_PARAMETER_RANGE(config->input_current_expected_ma, 0, MAX_CURRENT_MA, input_current_expected_ma, OWNER_MANAGER_PWR_PARSE_PACKET);
    CHECK_PARAMETER_RANGE(config->power_warning_total_mW, 0, config->input_current_expected_ma * config->input_voltage_expected_mv / 1000, power_warning_total_mW, OWNER_MANAGER_PWR_PARSE_PACKET);
    CHECK_PARAMETER_RANGE(config->power_critical_total_mW, 0, config->input_current_expected_ma * config->input_voltage_expected_mv / 1000, power_critical_total_mW, OWNER_MANAGER_PWR_PARSE_PACKET);
    CHECK_PARAMETER_RANGE(config->power_warning_reg_0_mW, 0, config->input_current_expected_ma * config->input_voltage_expected_mv / 1000, power_warning_reg_0_mW, OWNER_MANAGER_PWR_PARSE_PACKET);
    CHECK_PARAMETER_RANGE(config->power_critical_reg_0_mW, 0, config->input_current_expected_ma * config->input_voltage_expected_mv / 1000, power_critical_reg_0_mW, OWNER_MANAGER_PWR_PARSE_PACKET);
    CHECK_PARAMETER_RANGE(config->power_warning_reg_1_mW, 0, config->input_current_expected_ma * config->input_voltage_expected_mv / 1000, power_warning_reg_1_mW, OWNER_MANAGER_PWR_PARSE_PACKET);
    CHECK_PARAMETER_RANGE(config->power_critical_reg_1_mW, 0, config->input_current_expected_ma * config->input_voltage_expected_mv / 1000, power_critical_reg_1_mW, OWNER_MANAGER_PWR_PARSE_PACKET);
    #pragma GCC diagnostic pop
    power_budget.current_power_budget_mW = (config->input_current_expected_ma * config->input_voltage_expected_mv) / 1000;
    memcpy(&current_power_config, config, sizeof(m_pwr_cfg_pkt_limits_t));
    return STA_OK;
} 

static status_rep_t validate_regulator_config(const m_pwr_cfg_pkt_regulator_t* config) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wtype-limits"
    CHECK_PARAMETER_RANGE(config->output_voltage_expected_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV, output_voltage_expected_mv, OWNER_MANAGER_PWR_PARSE_PACKET);
    CHECK_PARAMETER_RANGE(config->current_limit_ma, 0, MAX_CURRENT_MA, current_limit_ma, OWNER_MANAGER_PWR_PARSE_PACKET);
    CHECK_PARAMETER_RANGE(config->power_limit_mW, 0, config->output_voltage_expected_mv * config->current_limit_ma / 1000, power_limit_mW, OWNER_MANAGER_PWR_PARSE_PACKET);
    if (config->reg_num > 1) {
        RETURN_INVALID_ARG(config->reg_num, reg_num, OWNER_MANAGER_PWR_PARSE_PACKET);
    }
    #pragma GCC diagnostic pop
    
    uint32_t current_power_mW = 0; 
    if(config->current_limit_ma >0 ){
        //use current if provided 
        current_power_mW = (config->output_voltage_expected_mv * config->current_limit_ma) / 1000;
    }else{
        current_power_mW = config->power_limit_mW;
    }

    uint32_t power_budget_remaining_mW = power_budget.current_power_budget_mW;
    if (config->reg_num == 0) {
        power_budget_remaining_mW -= power_budget.reg_1_budget_mW; 
    } else {
        power_budget_remaining_mW -= power_budget.reg_0_budget_mW; 
    }
    if (current_power_mW > power_budget_remaining_mW) {
        // ESP_LOGW(TAG, "Regulator %u power limit %u mW exceeds remaining power budget %u mW", config->reg_num, current_power_mW, power_budget_remaining_mW);
        RETURN_INVALID_ARG(current_power_mW, power_limit_mW, OWNER_MANAGER_PWR_PARSE_PACKET);
    }
    if (config->reg_num == 0) {
        power_budget.reg_0_budget_mW = current_power_mW;
    } else {
        power_budget.reg_1_budget_mW = current_power_mW;
    }
    power_budget.current_power_budget_use_mW = power_budget.reg_0_budget_mW + power_budget.reg_1_budget_mW;
    memcpy(&current_regulator_configs[config->reg_num], config, sizeof(m_pwr_cfg_pkt_regulator_t));
    return STA_OK;
}
    
static status_rep_t apply_power_config() {

    ESP_LOGI(TAG, "Applying power configuration");
    return STA_OK;
}

static status_rep_t apply_regulator_config(const m_pwr_cfg_pkt_regulator_t* config) {
    ESP_LOGI(TAG, "Applying regulator configuration for regulator %u", config->reg_num);
    STA_RET_ON_ERR(sys_pwr_enable_verg(config->reg_num, config->output_enable));
    STA_RET_ON_ERR(sys_pwr_set_verg_voltage(config->reg_num, config->output_voltage_expected_mv));
    STA_RET_ON_ERR(sys_pwr_set_verg_current_limit(config->reg_num, config->current_limit_ma));
    return STA_OK;
}

status_rep_t manager_pwr_process_packet(const uint8_t* packet_data, uint16_t packet_len){
    switch(packet_data[0]){ // Assuming first byte is packet type
        case MANAGER_PWR_PACKET_TYPE_POWER_CONFIG: {

            m_pwr_cfg_pkt_limits_t new_config;
            if(packet_len < 1 + sizeof(m_pwr_cfg_pkt_limits_t)) {
                ESP_LOGW(TAG, "Received power config packet with invalid length: %u",packet_len);
                return STA_C(PWE_ERR_PARSE_NOT_FOUND, OWNER_MANAGER_PWR_PARSE_PACKET, packet_len);
            }

            memcpy(&new_config, packet_data + 1, sizeof(m_pwr_cfg_pkt_limits_t));
            status_rep_t status = validate_power_config(&new_config);
            if (!STA_IS_OK(status)) {
                ESP_LOGW(TAG, "Power config validation failed");
                status.details.severity = 1;
                STA_RP(status);
            }
            status = apply_power_config();
            if (!STA_IS_OK(status)) {
                ESP_LOGW(TAG, "Applying power config failed");
                status.details.severity = 1;
                STA_P(status);
                STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_SET_PARAM, status.e_code));
            }
            
            break;
        }
        case MANAGER_PWR_PACKET_TYPE_REGULATOR_CONFIG: {
            m_pwr_cfg_pkt_regulator_t new_reg_config;
            if(packet_len < 1 + sizeof(m_pwr_cfg_pkt_regulator_t)) {
                ESP_LOGW(TAG, "Received regulator config packet with invalid length: %u",packet_len);
                return STA_C(PWE_ERR_PARSE_NOT_FOUND, OWNER_MANAGER_PWR_PARSE_PACKET, packet_len);
            }
            memcpy(&new_reg_config, packet_data + 1, sizeof(m_pwr_cfg_pkt_regulator_t));
            status_rep_t status = validate_regulator_config(&new_reg_config);
            if (!STA_IS_OK(status)) {
                ESP_LOGW(TAG, "Regulator config validation failed");
                status.details.severity = 1;
                STA_P(status);
            }
            status = apply_regulator_config(&new_reg_config);
            if (!STA_IS_OK(status)) {  
                ESP_LOGW(TAG, "Applying regulator config failed");
                status.details.severity = 1;
                STA_P(status);
                STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_SET_PARAM, status.e_code));
            } 
            break;
        }
        default:
            return STA_W(PWE_ERR_PARSE_NOT_FOUND, OWNER_MANAGER_PWR_PARSE_PACKET, packet_data[0]);
    }
    return STA_OK;
}
