#include "manager_power.h"
#include "manager_io.h"
#include "string.h"
#include "provider_voltage_regulator.h"
#include "provider_current_monitor.h"
#include "provider_gpio_expander.h"
#include "math.h"

#define TAG __FILE_NAME__
#define MAX_CURRENT_MA 5500
#define MAX_VOLTAGE_MV 20000
static bool _is_in_freeze_mode = false;

void manager_pwr_freeze_mode(bool freeze){
    _is_in_freeze_mode = freeze;
}

status_rep_t sys_pwr_set_bus_current_warning(uint8_t channel, int32_t current_mA){
    if (abs(current_mA) > MAX_CURRENT_MA) {
        ESP_LOGW(TAG, "Current warning limit out of range: %d mA", current_mA);
        STA_RP(STA_C(PWR_ERR_INVALID_PARAM, OWNER_MANAGER_PWR_SET_PARAM, current_mA));
    }
    status_rep_t status = p_current_monitor_set_warning(channel, (int32_t)current_mA);
    if(!STA_IS_OK(status)){
        if(status.e_code == PWR_ERR_DEVICE_NOT_FOUND){
            ESP_LOGW(TAG, "Current monitor device not found for setting current warning limit");
            STA_RP(STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_MANAGER_PWR_SET_PARAM, channel));
        }else if(status.e_code == PWR_ERR_INVALID_PARAM){
            ESP_LOGW(TAG, "Invalid parameter for setting current warning limit: channel %u, current %u mA", channel, current_mA);
            STA_RP(STA_C(PWR_ERR_INVALID_PARAM, OWNER_MANAGER_PWR_SET_PARAM, channel));
        }
        ESP_LOGW(TAG, "Failed to set current warning limit - internal driver error");
        status.details.severity = 1;
        STA_P(status);
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_SET_PARAM, channel));
    }
    return STA_OK;
}
status_rep_t sys_pwr_set_bus_current_critical(uint8_t channel, int32_t current_mA){
    if (abs(current_mA) > MAX_CURRENT_MA) {
        ESP_LOGW(TAG, "Current critical limit out of range: %d mA", current_mA);
        STA_RP(STA_C(PWR_ERR_INVALID_PARAM, OWNER_MANAGER_PWR_SET_PARAM, current_mA));
    }
    status_rep_t status = p_current_monitor_set_crit(channel, current_mA);
    if(!STA_IS_OK(status)){
        if(status.e_code == PWR_ERR_DEVICE_NOT_FOUND){
            ESP_LOGW(TAG, "Current monitor device not found for setting current critical limit");
            STA_RP(STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_MANAGER_PWR_SET_PARAM, channel));
        }else if(status.e_code == PWR_ERR_INVALID_PARAM){
            ESP_LOGW(TAG, "Invalid parameter for setting current critical limit: channel %u, current %u mA", channel, current_mA);
            STA_RP(STA_C(PWR_ERR_INVALID_PARAM, OWNER_MANAGER_PWR_SET_PARAM, channel));
        }
        ESP_LOGW(TAG, "Failed to set current critical limit - internal driver error");
        status.details.severity = 1;
        STA_P(status);
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_SET_PARAM, channel));
    }
    return STA_OK;
}

status_rep_t sys_pwr_set_bus_power_warning(uint8_t channel, int32_t power_mW){
    uint32_t voltage_mv = 0;
    status_rep_t status = p_current_monitor_get_voltage(channel, &voltage_mv, !_is_in_freeze_mode);
    if(!STA_IS_OK(status)){
        if(status.e_code == PWR_ERR_DEVICE_NOT_FOUND){
            ESP_LOGW(TAG, "Current monitor device not found for setting power warning limit");
            STA_RP(STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_MANAGER_PWR_SET_PARAM, channel));
        }else if(status.e_code == PWR_ERR_INVALID_PARAM){
            ESP_LOGW(TAG, "Invalid parameter for setting power warning limit: channel %u", channel);
            STA_RP(STA_C(PWR_ERR_INVALID_PARAM, OWNER_MANAGER_PWR_SET_PARAM, channel));
        }
        ESP_LOGW(TAG, "Failed to get voltage for calculating current warning limit - internal driver error");
        status.details.severity = 1;
        STA_P(status);
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_SET_PARAM, channel));
    }
    int32_t current = power_mW * 1000 / (voltage_mv == 0 ? 1 : voltage_mv);
    return sys_pwr_set_bus_current_warning(channel, current);
}
status_rep_t sys_pwr_set_bus_power_critical(uint8_t channel, int32_t power_mW){
    uint32_t voltage_mv = 0;
    status_rep_t status = p_current_monitor_get_voltage(channel, &voltage_mv, !_is_in_freeze_mode);
    if(!STA_IS_OK(status)){
        if(status.e_code == PWR_ERR_DEVICE_NOT_FOUND){
            ESP_LOGW(TAG, "Current monitor device not found for setting power critical limit");
            STA_RP(STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_MANAGER_PWR_SET_PARAM, channel));
        }else if(status.e_code == PWR_ERR_INVALID_PARAM){
            ESP_LOGW(TAG, "Invalid parameter for setting power critical limit: channel %u", channel);
            STA_RP(STA_C(PWR_ERR_INVALID_PARAM, OWNER_MANAGER_PWR_SET_PARAM, channel));
        }
        ESP_LOGW(TAG, "Failed to get voltage for calculating current critical limit - internal driver error");
        status.details.severity = 1;
        STA_P(status);
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_SET_PARAM, channel));
    }
    int32_t current = power_mW * 1000 / (voltage_mv == 0 ? 1 : voltage_mv);
    return sys_pwr_set_bus_current_critical(channel, current);
}

status_rep_t sys_pwr_get_bus_voltage(uint8_t channel, uint32_t *voltage_mV){
    status_rep_t status = p_current_monitor_get_voltage(channel, voltage_mV, !_is_in_freeze_mode);
    if(!STA_IS_OK(status)){
        if(status.e_code == PWR_ERR_DEVICE_NOT_FOUND){
            ESP_LOGW(TAG, "Current monitor device not found for reading bus voltage");
            STA_RP(STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_MANAGER_PWR_READ, channel));
        }else if(status.e_code == PWR_ERR_INVALID_PARAM){
            ESP_LOGW(TAG, "Invalid parameter for reading bus voltage: channel %u", channel);
            STA_RP(STA_C(PWR_ERR_INVALID_PARAM, OWNER_MANAGER_PWR_READ, channel));
        }
        ESP_LOGW(TAG, "Failed to read bus voltage - internal driver error");
        status.details.severity = 1;
        STA_P(status);
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_READ, channel));
    }
    return STA_OK;
}
status_rep_t sys_pwr_get_bus_current(uint8_t channel, int32_t *current_mA){
    status_rep_t status = p_current_monitor_get_current(channel, current_mA, !_is_in_freeze_mode);
    if(!STA_IS_OK(status)){
        if(status.e_code == PWR_ERR_DEVICE_NOT_FOUND){
            ESP_LOGW(TAG, "Current monitor device not found for reading bus current");
            STA_RP(STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_MANAGER_PWR_READ, channel));
        }else if(status.e_code == PWR_ERR_INVALID_PARAM){
            ESP_LOGW(TAG, "Invalid parameter for reading bus current: channel %u", channel);
            STA_RP(STA_C(PWR_ERR_INVALID_PARAM, OWNER_MANAGER_PWR_READ, channel));
        }
        ESP_LOGW(TAG, "Failed to read bus current - internal driver error");
        status.details.severity = 1;
        STA_P(status);
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_READ, channel));
    }
    return STA_OK;
}
    
status_rep_t manager_pwr_init(){
    return STA_OK;
} 

status_rep_t manager_pwr_add_cb(manager_pwr_cb_type_e cb_type, void (*handler)(void *), void* ctx){
    status_rep_t status = STA_OK;
    switch(cb_type){
        case MANAGER_PWR_CB_REG0_OVP:
            status = p_vreg_register_ovp_callback(0, handler, ctx);
            break;
        case MANAGER_PWR_CB_REG0_OCP:
            status = p_vreg_register_ocp_callback(0, handler, ctx);
            break;
        case MANAGER_PWR_CB_REG0_SCP:
            status = p_vreg_register_scp_callback(0, handler, ctx);
            break;
        case MANAGER_PWR_CB_REG1_OVP:
            status = p_vreg_register_ovp_callback(1, handler, ctx);
            break;
        case MANAGER_PWR_CB_REG1_OCP:
            status = p_vreg_register_ocp_callback(1, handler, ctx);
            break;
        case MANAGER_PWR_CB_REG1_SCP:
            status = p_vreg_register_scp_callback(1, handler, ctx);
            break;
        case MANAGER_PWR_CB_POWER_CH0_WARNING:
            status = p_current_monitor_register_warning_callback(0, handler, ctx);
            break;
        case MANAGER_PWR_CB_POWER_CH0_CRITICAL:
            status = p_current_monitor_register_critical_callback(0, handler, ctx);
            break;
        case MANAGER_PWR_CB_POWER_CH1_WARNING:
            status = p_current_monitor_register_warning_callback(1, handler, ctx);
            break;
        case MANAGER_PWR_CB_POWER_CH1_CRITICAL:
            status = p_current_monitor_register_critical_callback(1, handler, ctx);
            break;
        case MANAGER_PWR_CB_POWER_CH2_WARNING:
            status = p_current_monitor_register_warning_callback(2, handler, ctx);
            break;
        case MANAGER_PWR_CB_POWER_CH2_CRITICAL:
            status = p_current_monitor_register_critical_callback(2, handler, ctx);
            break; 
        default:
            ESP_LOGW(TAG, "Invalid callback type for registration: %u", cb_type);
    }
    if(!STA_IS_OK(status)){
        ESP_LOGW(TAG, "Failed to register callback - internal driver error");
        status.details.severity = 1;
        //!!!return driver error as well as generic error from manager
        STA_P(status);
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_REGISTER_CALLBACK, status.e_code));
    }
    return STA_OK;
}

status_rep_t sys_pwr_set_verg_voltage(bool regulator_id, uint32_t voltage_mv){
    if (voltage_mv > MAX_VOLTAGE_MV) {
        ESP_LOGW(TAG, "Voltage limit out of range: %u mV", voltage_mv);
        STA_RP(STA_C(PWR_ERR_INVALID_PARAM, OWNER_MANAGER_PWR_SET_PARAM, voltage_mv));
    }
    status_rep_t status = p_vreg_set_voltage(regulator_id, voltage_mv);
    if(!STA_IS_OK(status)){
        if(status.e_code == PWR_ERR_DEVICE_NOT_FOUND){
            ESP_LOGW(TAG, "Voltage regulator device not found for setting voltage");
            STA_RP(STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_MANAGER_PWR_SET_PARAM, regulator_id));
        }
        ESP_LOGW(TAG, "Failed to set voltage - internal driver error");
        status.details.severity = 1;
        STA_P(status);
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_SET_PARAM, status.e_code));
    }
    return STA_OK;
}

status_rep_t sys_pwr_set_verg_current_limit(bool regulator_id, uint32_t current_ma){
    if (current_ma > MAX_CURRENT_MA) {
        ESP_LOGW(TAG, "Current limit out of range: %u mA", current_ma);
        STA_RP(STA_C(PWR_ERR_INVALID_PARAM, OWNER_MANAGER_PWR_SET_PARAM, current_ma));
    }
    status_rep_t status = p_vreg_set_current_limit(regulator_id, current_ma);
    if(!STA_IS_OK(status)){
        if(status.e_code == PWR_ERR_DEVICE_NOT_FOUND){
            ESP_LOGW(TAG, "Voltage regulator device not found for setting current limit");
            STA_RP(STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_MANAGER_PWR_SET_PARAM, regulator_id));
        }
        ESP_LOGW(TAG, "Failed to set current limit - internal driver error");
        status.details.severity = 1;
        STA_P(status);
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_SET_PARAM, status.e_code));
    }
    return STA_OK;
}
status_rep_t sys_pwr_enable_verg(bool regulator_id, bool enable){
    status_rep_t status = p_vreg_en(regulator_id, enable);
    if(!STA_IS_OK(status)){
        if(status.e_code == PWR_ERR_DEVICE_NOT_FOUND){
            ESP_LOGW(TAG, "Voltage regulator device not found for setting enable state");
            STA_RP(STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_MANAGER_PWR_SET_PARAM, regulator_id));
        }
        ESP_LOGW(TAG, "Failed to set enable state - internal driver error");
        status.details.severity = 1;
        STA_P(status);
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_SET_PARAM, status.e_code));
    }
    return STA_OK;
}