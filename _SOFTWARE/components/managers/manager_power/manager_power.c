#include "manager_power.h"
#include "manager_io.h"
#include "string.h"
#include "provider_voltage_regulator.h"
#include "provider_current_monitor.h"
#include "provider_power_delivery.h"
#include "provider_gpio_expander.h"
#include "math.h"
#include "sdkconfig.h"


#define TAG __FILE_NAME__
#define MAX_CURRENT_MA 5500
#define MAX_VOLTAGE_MV 20000
#define MAX_POWER_MW 120000
#define MIN_VOLTAGE_MV 3500
static bool _is_in_freeze_mode = false;
void manager_pwr_freeze(bool freeze){
    _is_in_freeze_mode = freeze;
}

#undef OWNER
#define OWNER OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR
status_rep_t sys_pwr_set_bus_current_warning(uint8_t channel, int32_t current_mA){
    #if !CONFIG_CONNECT_INA3221
    STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR, channel));
    #endif
    CHECK_ARG_RP(current_mA, -MAX_CURRENT_MA, MAX_CURRENT_MA, 0); 
    CHECK_ARG_RP(channel, 0, 3, 0);
    status_rep_t status = p_current_monitor_set_warning(channel, current_mA);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR , channel));
    }
    STA_RP_ON_ERR(status);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_MANAGER_PWR_CONFIG_PD
status_rep_t sys_pwr_set_pd_voltage_current(uint32_t voltage_mv, uint32_t current_ma){
#if !CONFIG_CONNECT_AP33772S
    STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, 0));
#endif
    CHECK_ARG_RP(current_ma, 1, MAX_CURRENT_MA, 0);
    status_rep_t status = p_power_delivery_set_voltage_and_current(voltage_mv, current_ma);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_CONFIG_PD, status.e_code));
    }
    STA_RP_ON_ERR(status);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_MANAGER_PWR_READ_PD_VOLTAGE
status_rep_t sys_pwr_get_pd_voltage(uint32_t *voltage_mv){
#if !CONFIG_CONNECT_AP33772S
    STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, 0));
#endif
    CHECK_NOT_NULL_R(voltage_mv);
    status_rep_t status = p_power_delivery_get_voltage(voltage_mv);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_READ_PD_VOLTAGE, status.e_code));
    }
    STA_RP_ON_ERR(status);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_MANAGER_PWR_READ_PD_CURRENT
status_rep_t sys_pwr_get_pd_current(int32_t *current_ma){
#if !CONFIG_CONNECT_AP33772S
    STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, 0));
#endif
    CHECK_NOT_NULL_R(current_ma);
    status_rep_t status = p_power_delivery_get_current(current_ma);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_READ_PD_CURRENT, status.e_code));
    }
    STA_RP_ON_ERR(status);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR
status_rep_t sys_pwr_set_bus_current_critical(uint8_t channel, int32_t current_mA){
    #if !CONFIG_CONNECT_INA3221
    STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR, channel));
    #endif
    CHECK_ARG_RP(current_mA, -MAX_CURRENT_MA, MAX_CURRENT_MA, 0); 
    CHECK_ARG_RP(channel, 0, 3, 0);
    status_rep_t status = p_current_monitor_set_crit(channel, current_mA);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR, channel));
    }
    STA_RP_ON_ERR(status);
    return STA_OK;
}


#undef OWNER
#define OWNER OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR
status_rep_t sys_pwr_set_bus_power_warning(uint8_t channel, int32_t power_mW){
    #if !CONFIG_CONNECT_INA3221
    STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR, channel));
    #endif
    CHECK_ARG_RP(power_mW, 1, MAX_POWER_MW, 0);
    uint32_t voltage_mv = 0;
    status_rep_t status = p_current_monitor_get_voltage(channel, &voltage_mv, true);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR, channel));
    }
    int32_t current = power_mW * 1000 / (voltage_mv == 0 ? 1 : voltage_mv);
    CHECK_ARG_RP(current, 1, MAX_CURRENT_MA, 0);
    status = sys_pwr_set_bus_current_warning(channel, current);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR, channel));
    }
    STA_RP_ON_ERR(status);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR
status_rep_t sys_pwr_set_bus_power_critical(uint8_t channel, int32_t power_mW){
    #if !CONFIG_CONNECT_INA3221
    STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR, channel));
    #endif
    CHECK_ARG_RP(power_mW, 1, MAX_POWER_MW, 0);
    uint32_t voltage_mv = 0;
    status_rep_t status = p_current_monitor_get_voltage(channel, &voltage_mv, !_is_in_freeze_mode);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR, channel));
    }
    int32_t current = power_mW * 1000 / (voltage_mv == 0 ? 1 : voltage_mv);
    CHECK_ARG_RP(current, 1, MAX_CURRENT_MA, 0);
    status = sys_pwr_set_bus_current_critical(channel, current);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR, channel));
    }
    STA_RP_ON_ERR(status);
    return STA_OK;
}


#undef OWNER
#define OWNER OWNER_MANAGER_PWR_READ_CURRENT_MONITOR_VOLTAGE
status_rep_t sys_pwr_get_bus_voltage(uint8_t channel, uint32_t *voltage_mV){
    #if !CONFIG_CONNECT_INA3221
    STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER_MANAGER_PWR_READ_CURRENT_MONITOR_VOLTAGE, channel));
    #endif
    CHECK_ARG_RP(channel, 0, 3, 0);
    CHECK_NOT_NULL_R(voltage_mV);
    status_rep_t status = p_current_monitor_get_voltage(channel, voltage_mV, !_is_in_freeze_mode);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_READ_CURRENT_MONITOR_VOLTAGE, channel));
    }
    STA_RP_ON_ERR(status);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_MANAGER_PWR_READ_CURRENT_MONITOR_CURRENT
status_rep_t sys_pwr_get_bus_current(uint8_t channel, int32_t *current_mA){
    #if !CONFIG_CONNECT_INA3221
    STA_RP(STA_C(PWR_ERR_FEATURE_UNSUPPORTED, OWNER_MANAGER_PWR_READ_CURRENT_MONITOR_CURRENT, channel));
    #endif
    CHECK_ARG_RP(channel, 0, 3, 0);
    CHECK_NOT_NULL_R(current_mA);
    status_rep_t status = p_current_monitor_get_current(channel, current_mA, !_is_in_freeze_mode);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_READ_CURRENT_MONITOR_CURRENT, channel));
    }
    STA_RP_ON_ERR(status);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR
status_rep_t sys_pwr_current_monitor_reset(void) {
    #if !CONFIG_CONNECT_INA3221
    STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR, 0));
    #endif
    status_rep_t status = p_current_monitor_reset();
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_CONFIG_CURRENT_MONITOR, status.e_code));
    }
    STA_RP_ON_ERR(status);
    ESP_LOGI(TAG, "Current monitor reset successfully");
    return STA_OK;
}
    
status_rep_t manager_pwr_init(){
    return STA_OK;
} 

#undef OWNER
#define OWNER OWNER_MANAGER_PWR_REGISTER_CALLBACK
status_rep_t manager_pwr_add_cb(manager_pwr_cb_type_e cb_type, void (*handler)(void *), void* ctx){
    status_rep_t status = STA_OK;
    switch(cb_type){
        case MANAGER_PWR_CB_REG0_OVP:
#if !CONFIG_CONNECT_TPS55289_0
            return STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, cb_type);
#else
            status = p_vreg_register_ovp_callback(0, handler, ctx);
            break;
#endif
        case MANAGER_PWR_CB_REG0_OCP:
#if !CONFIG_CONNECT_TPS55289_0
            return STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, cb_type);
#else
            status = p_vreg_register_ocp_callback(0, handler, ctx);
            break;
#endif
        case MANAGER_PWR_CB_REG0_SCP:
#if !CONFIG_CONNECT_TPS55289_0
            return STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, cb_type);
#else
            status = p_vreg_register_scp_callback(0, handler, ctx);
            break;
#endif
        case MANAGER_PWR_CB_REG1_OVP:
#if !CONFIG_CONNECT_TPS55289_1
            return STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, cb_type);
#else
            status = p_vreg_register_ovp_callback(1, handler, ctx);
            break;
#endif
        case MANAGER_PWR_CB_REG1_OCP:
#if !CONFIG_CONNECT_TPS55289_1
            return STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, cb_type);
#else
            status = p_vreg_register_ocp_callback(1, handler, ctx);
            break;
#endif
        case MANAGER_PWR_CB_REG1_SCP:
#if !CONFIG_CONNECT_TPS55289_1
            return STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, cb_type);
#else
            status = p_vreg_register_scp_callback(1, handler, ctx);
            break;
#endif
        case MANAGER_PWR_CB_CURRENT_REG0_WARNING:
#if !CONFIG_CONNECT_INA3221
            return STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, cb_type);
#else
            status = p_current_monitor_register_warning_callback(0, handler, ctx);
            break;
#endif
        case MANAGER_PWR_CB_CURRENT_REG0_CRITICAL:
#if !CONFIG_CONNECT_INA3221
            return STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, cb_type);
#else
            status = p_current_monitor_register_critical_callback(0, handler, ctx);
            break;
#endif
        case MANAGER_PWR_CB_CURRENT_SYS_WARNING:
#if !CONFIG_CONNECT_INA3221
            return STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, cb_type);
#else
            status = p_current_monitor_register_warning_callback(1, handler, ctx);
            break;
#endif
        case MANAGER_PWR_CB_CURRENT_SYS_CRITICAL:
#if !CONFIG_CONNECT_INA3221
            return STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, cb_type);
#else
            status = p_current_monitor_register_critical_callback(1, handler, ctx);
            break;
#endif
        case MANAGER_PWR_CB_CURRENT_REG1_WARNING:
#if !CONFIG_CONNECT_INA3221
            return STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, cb_type);
#else
            status = p_current_monitor_register_warning_callback(2, handler, ctx);
            break;
#endif
        case MANAGER_PWR_CB_CURRENT_REG1_CRITICAL:
#if !CONFIG_CONNECT_INA3221
            return STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, cb_type);
#else
            status = p_current_monitor_register_critical_callback(2, handler, ctx);
            break;
#endif 
        default:
            ESP_LOGW(TAG, "Invalid callback type for registration: %u", cb_type);
            return STA_C(ERR_INVALID_ARG, OWNER_MANAGER_PWR_REGISTER_CALLBACK, cb_type);
    }
    STA_RP_ON_ERR(status);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_REGISTER_CALLBACK, status.e_code));
    }
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_MANAGER_PWR_SET_PARAM_VREG
status_rep_t sys_pwr_set_verg_voltage(bool regulator_id, uint32_t voltage_mv){
    if (regulator_id == 1){
#if !CONFIG_CONNECT_TPS55289_1
        STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, 1));
#endif
        CHECK_ARG_RP(voltage_mv, MIN_VOLTAGE_MV, CONFIG_TPS55289_1_MAX_VOLTAGE, 0);
    }else{
#if !CONFIG_CONNECT_TPS55289_0
        STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, 0));
#endif
        CHECK_ARG_RP(voltage_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV, 0);
    }
    status_rep_t status = p_vreg_set_voltage(regulator_id, voltage_mv);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_SET_PARAM_VREG, status.e_code));
    }
    STA_RP_ON_ERR(status);
    return STA_OK;
}


#undef OWNER
#define OWNER OWNER_MANAGER_PWR_SET_PARAM_VREG
status_rep_t sys_pwr_set_verg_current_limit(bool regulator_id, uint32_t current_ma){
    if (regulator_id == 1) {
#if !CONFIG_CONNECT_TPS55289_1
        STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, 1));
#endif
    } else {
#if !CONFIG_CONNECT_TPS55289_0
        STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, 0));
#endif
    }
    CHECK_ARG_RP(current_ma, 1, MAX_CURRENT_MA, 0);    
    status_rep_t status = p_vreg_set_current_limit(regulator_id, current_ma);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_SET_PARAM_VREG, status.e_code));
    }
    STA_RP_ON_ERR(status);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_MANAGER_PWR_EN_VREG
status_rep_t sys_pwr_enable_verg(bool regulator_id, bool enable){
    if (regulator_id == 1) {
#if !CONFIG_CONNECT_TPS55289_1
        STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, 1));
#endif
    } else {
#if !CONFIG_CONNECT_TPS55289_0
        STA_RP(STA_W(PWR_ERR_FEATURE_UNSUPPORTED, OWNER, 0));
#endif
    }
    status_rep_t status = p_vreg_en(regulator_id, enable);
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_EN_VREG, status.e_code));
    }
    STA_RP_ON_ERR(status);
    return STA_OK;
}