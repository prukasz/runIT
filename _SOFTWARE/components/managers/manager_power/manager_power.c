#include "manager_power.h"
#include "manager_io.h"
#include "manager_power_config.h"
#include "string.h"
#include "provider_voltage_regulator.h"
#include "provider_current_monitor.h"
#include "provider_gpio_expander.h"
#include "tca6424a_mock.h"
#include "tps55289_mock.h"
// #include "rik_shared.h"

#define TAG __FILE_NAME__

#define IN_RANGE(val, min, max) ((val) >= (min) && (val) <= (max)) 
#define RETURN_INVALID_ARG(val, name, owner) do { \
    ESP_LOGE(TAG, "Invalid argument %s: %lu", #name, (uint32_t)(val)); \
    return STA_C(ESP_ERR_INVALID_ARG, owner, (val)); \
} while (0)

#define CHECK_PARAMETER_RANGE(val, min, max, name, owner) do { \
    if (!IN_RANGE((val), (min), (max))) { \
        RETURN_INVALID_ARG((val), (name), (owner)); \
    } \
} while (0)


#define MAX_CURRENT_MA 5500
#define MIN_VOLTAGE_MV 4500
#define MAX_VOLTAGE_MV 20000


static sys_pwr_cfg_power_t current_power_config;
static sys_pwr_cfg_regulator_t current_regulator_configs[2]; 

static struct{
    uint32_t reg_1_budget_mW;
    uint32_t reg_0_budget_mW;
    uint32_t current_power_budget_mW;
    uint32_t current_power_budget_use_mW;
}power_budget;

static manager_pwr_config_t manager_config;

static status_rep_t validate_power_config(const sys_pwr_cfg_power_t* config) {
    //CHECK_PARAMETER_RANGE(val, min, max, name, owner)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wtype-limits"
    CHECK_PARAMETER_RANGE(config->input_voltage_expected_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV, input_voltage_expected_mv, OWNER_MANAGER_PWR);
    CHECK_PARAMETER_RANGE(config->input_voltage_warning_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV, input_voltage_warning_mv, OWNER_MANAGER_PWR);
    CHECK_PARAMETER_RANGE(config->input_voltage_critical_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV, input_voltage_critical_mv, OWNER_MANAGER_PWR);
    CHECK_PARAMETER_RANGE(config->input_current_expected_ma, 0, MAX_CURRENT_MA, input_current_expected_ma, OWNER_MANAGER_PWR);
    CHECK_PARAMETER_RANGE(config->power_warning_total_mW, 0, config->input_current_expected_ma * config->input_voltage_expected_mv / 1000, power_warning_total_mW, OWNER_MANAGER_PWR);
    CHECK_PARAMETER_RANGE(config->power_critical_total_mW, 0, config->input_current_expected_ma * config->input_voltage_expected_mv / 1000, power_critical_total_mW, OWNER_MANAGER_PWR);
    CHECK_PARAMETER_RANGE(config->power_warning_reg_0_mW, 0, config->input_current_expected_ma * config->input_voltage_expected_mv / 1000, power_warning_reg_0_mW, OWNER_MANAGER_PWR);
    CHECK_PARAMETER_RANGE(config->power_critical_reg_0_mW, 0, config->input_current_expected_ma * config->input_voltage_expected_mv / 1000, power_critical_reg_0_mW, OWNER_MANAGER_PWR);
    CHECK_PARAMETER_RANGE(config->power_warning_reg_1_mW, 0, config->input_current_expected_ma * config->input_voltage_expected_mv / 1000, power_warning_reg_1_mW, OWNER_MANAGER_PWR);
    CHECK_PARAMETER_RANGE(config->power_critical_reg_1_mW, 0, config->input_current_expected_ma * config->input_voltage_expected_mv / 1000, power_critical_reg_1_mW, OWNER_MANAGER_PWR);  
    #pragma GCC diagnostic pop
    power_budget.current_power_budget_mW = (config->input_current_expected_ma * config->input_voltage_expected_mv) / 1000;
    memcpy(&current_power_config, config, sizeof(sys_pwr_cfg_power_t));
    return STA_OK;
} 

static status_rep_t validate_regulator_config(const sys_pwr_cfg_regulator_t* config) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wtype-limits"
    CHECK_PARAMETER_RANGE(config->output_voltage_expected_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV, output_voltage_expected_mv, OWNER_MANAGER_PWR);
    CHECK_PARAMETER_RANGE(config->current_limit_ma, 0, MAX_CURRENT_MA, current_limit_ma, OWNER_MANAGER_PWR);
    CHECK_PARAMETER_RANGE(config->power_limit_mW, 0, config->output_voltage_expected_mv * config->current_limit_ma / 1000, power_limit_mW, OWNER_MANAGER_PWR);
    if (config->reg_num > 1) {
        RETURN_INVALID_ARG(config->reg_num, reg_num, OWNER_MANAGER_PWR);
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
        RETURN_INVALID_ARG(current_power_mW, power_limit_mW, OWNER_MANAGER_PWR);
    }
    if (config->reg_num == 0) {
        power_budget.reg_0_budget_mW = current_power_mW;
    } else {
        power_budget.reg_1_budget_mW = current_power_mW;
    }
    power_budget.current_power_budget_use_mW = power_budget.reg_0_budget_mW + power_budget.reg_1_budget_mW;
    memcpy(&current_regulator_configs[config->reg_num], config, sizeof(sys_pwr_cfg_regulator_t));
    return STA_OK;
}
    
static status_rep_t apply_power_config() {
    // fill with wrappers behaviour and others 
    ESP_LOGI(TAG, "Applying power configuration");
    return STA_OK;
}

static status_rep_t apply_regulator_config(const sys_pwr_cfg_regulator_t* config) {
    ESP_LOGI(TAG, "Applying regulator configuration for regulator %u", config->reg_num);
    STA_RET_ON_ERR(sys_pwr_en_reg(config->reg_num, config->output_enable));
    STA_RET_ON_ERR(sys_pwr_set_reg_voltage(config->reg_num, config->output_voltage_expected_mv));
    STA_RET_ON_ERR(sys_pwr_set_reg_current_limit(config->reg_num, config->current_limit_ma));
    return STA_OK;
}

status_rep_t manager_pwr_process_packet(const uint8_t* packet_data, uint16_t packet_len){
    switch(packet_data[0]){ // Assuming first byte is packet type
        case MANAGER_PWR_PACKET_TYPE_POWER_CONFIG: {
            sys_pwr_cfg_power_t new_config;
            if(packet_len < 1 + sizeof(sys_pwr_cfg_power_t)) {
                ESP_LOGW(TAG, "Received power config packet with invalid length: %u",packet_len);
                return STA_C(ESP_ERR_INVALID_SIZE, OWNER_MANAGER_PWR, packet_len);
            }
            memcpy(&new_config, packet_data + 1, sizeof(sys_pwr_cfg_power_t));
            STA_RET_ON_ERR(validate_power_config(&new_config));
            STA_RET_ON_ERR(apply_power_config());
            break;
        }
        case MANAGER_PWR_PACKET_TYPE_REGULATOR_CONFIG: {
            sys_pwr_cfg_regulator_t new_reg_config;
            if(packet_len < 1 + sizeof(sys_pwr_cfg_regulator_t)) {
                ESP_LOGW(TAG, "Received regulator config packet with invalid length: %u",packet_len);
                return STA_C(ESP_ERR_INVALID_SIZE, OWNER_MANAGER_PWR, packet_len);
            }
            memcpy(&new_reg_config, packet_data + 1, sizeof(sys_pwr_cfg_regulator_t));
            STA_RET_ON_ERR(validate_regulator_config(&new_reg_config));
            STA_RET_ON_ERR(apply_regulator_config(&new_reg_config));
            break;
        }
        default:
            return STA_C(ESP_ERR_NOT_FOUND, OWNER_MANAGER_PWR, packet_data[0]);
    }
    return STA_OK;
}

status_rep_t manager_pwr_init(manager_pwr_config_t *config){
    manager_config = *config;

    /****init current monitor and regulators ****/
    sys_pwr_init_monitor(config->power_monitor_handle);
    sys_pwr_init_reg(config->reg_driver_handle_0, config->reg_driver_handle_1);
    /****init current monitor and regulators ****/

    return STA_OK;
} 


tca6424a_mock_pin_cfg_t tps0_fault = {
    .pin_mask = (1 << 1), // IO_TCA_REGA_INT
    .level = false
};
tca6424a_mock_pin_cfg_t tps1_fault = {
    .pin_mask = (1 << 2), // IO_TCA_REGB_INT
    .level = false
};
tca6424a_mock_pin_cfg_t tps0_no_fault = {
    .pin_mask = (1 << 1), // IO_TCA_REGA_INT
    .level = true
};
tca6424a_mock_pin_cfg_t tps1_no_fault = {
    .pin_mask = (1 << 2), // IO_TCA_REGB_INT
    .level = true
};

void manager_pwr_mocks_link(){
    tps_mock_set_intr_callbacks(0x74, &tca_mock_set_pin_level, &tps0_fault, &tca_mock_set_pin_level, &tps0_no_fault);
    tps_mock_set_intr_callbacks(0x75, &tca_mock_set_pin_level, &tps1_fault, &tca_mock_set_pin_level, &tps1_no_fault);

}

