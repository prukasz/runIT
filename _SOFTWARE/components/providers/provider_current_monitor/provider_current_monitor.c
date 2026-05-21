#include "provider_current_monitor.h"
#include "ina3221.h"

#define TAG __FILE_NAME__

static ina3221_handle_t _ina_handle = NULL;

status_rep_t sys_pwr_init_monitor(void* handle)
{
    if (!handle) {
        return STA_C(ESP_ERR_INVALID_ARG, OWNER_PROVIDER_CURRENT_MONITOR_INIT, 0);
    }

    _ina_handle = (ina3221_handle_t)handle;
    
    STA_RET_ON_ESP_ERR(ina3221_set_options(_ina_handle, 1, 1 ,1), OWNER_PROVIDER_CURRENT_MONITOR_INIT, _ina_handle->i2c_device_config.device_address);
    
    STA_RET_ON_ESP_ERR(ina3221_enable_latch_pin(_ina_handle, true, true),
    OWNER_PROVIDER_CURRENT_MONITOR_INIT, _ina_handle->i2c_device_config.device_address);
    STA_RET_ON_ESP_ERR(ina3221_set_warning_alert(_ina_handle, 0, 200),
    OWNER_PROVIDER_CURRENT_MONITOR_INIT, _ina_handle->i2c_device_config.device_address);
    STA_RET_ON_ESP_ERR(ina3221_set_critical_alert(_ina_handle, 0, 1000),
    OWNER_PROVIDER_CURRENT_MONITOR_INIT, _ina_handle->i2c_device_config.device_address);
    return STA_OK;
}

static status_rep_t _calculate_limit_ma(uint8_t channel, uint32_t power_mw, uint32_t expected_voltage_mv, uint32_t *out_limit_ma)
{
    if (!_ina_handle) {
        return STA_C(ESP_ERR_INVALID_STATE, OWNER_PROVIDER_CURRENT_MONITOR_SET_LIMITS, channel);
    }

    if (expected_voltage_mv == 0) {
        uint32_t bus_mv = 0;
        STA_RET_ON_ERR(sys_pwr_get_voltage(channel, &bus_mv, true));
        expected_voltage_mv = (uint32_t)bus_mv;
    }

    if (expected_voltage_mv == 0) {
        ESP_LOGE(TAG, "Cannot calculate limit for CH%d: Bus voltage is 0mV", channel);
        return STA_C(ESP_ERR_INVALID_ARG, OWNER_PROVIDER_CURRENT_MONITOR_SET_LIMITS, channel);
    }

    *out_limit_ma = (power_mw * 1000) / expected_voltage_mv;
    return STA_OK;
}

static status_rep_t _set_alert(uint8_t channel, uint32_t power_mw, uint32_t expected_voltage_mv, bool is_critical)
{
    uint32_t limit_ma = 0;
    STA_RET_ON_ERR(_calculate_limit_ma(channel, power_mw, expected_voltage_mv, &limit_ma));
    
    ESP_LOGI(TAG, "Setting %s Limit for CH%d: %lu mA (%lu mW at %lu mV)", 
             is_critical ? "CRIT" : "WARN", channel, limit_ma, power_mw, expected_voltage_mv);

    if (is_critical) {
        STA_RET_ON_ESP_ERR(ina3221_set_critical_alert(_ina_handle, channel, limit_ma), OWNER_PROVIDER_CURRENT_MONITOR_SET_LIMITS, channel);
    } else {
        STA_RET_ON_ESP_ERR(ina3221_set_warning_alert(_ina_handle, channel, limit_ma), OWNER_PROVIDER_CURRENT_MONITOR_SET_LIMITS, channel);
    }
    return STA_OK;
}

// --- REG 0 (TPS0) ---
status_rep_t sys_pwr_set_warning_reg_0(uint32_t power_mw, uint32_t expected_voltage_mv) {
    return _set_alert(INA_BUS_TPS0, power_mw, expected_voltage_mv, false);
}

status_rep_t sys_pwr_set_crit_reg_0(uint32_t power_mw, uint32_t expected_voltage_mv) {
    return _set_alert(INA_BUS_TPS0, power_mw, expected_voltage_mv, true);
}

// --- REG 1 (TPS1) ---
status_rep_t sys_pwr_set_warning_reg_1(uint32_t power_mw, uint32_t expected_voltage_mv) {
    return _set_alert(INA_BUS_TPS1, power_mw, expected_voltage_mv, false);
}

status_rep_t sys_pwr_set_crit_reg_1(uint32_t power_mw, uint32_t expected_voltage_mv) {
    return _set_alert(INA_BUS_TPS1, power_mw, expected_voltage_mv, true);
}

// --- TOTAL (VSUP) ---
status_rep_t sys_pwr_set_warning_total(uint32_t power_mw, uint32_t expected_voltage_mv) {
    return _set_alert(INA_BUS_VSUP, power_mw, expected_voltage_mv, false);
}

status_rep_t sys_pwr_set_crit_total(uint32_t power_mw, uint32_t expected_voltage_mv) {
    return _set_alert(INA_BUS_VSUP, power_mw, expected_voltage_mv, true);
}

    
status_rep_t sys_pwr_get_voltage(uint8_t bus_num, uint32_t* voltage_mv, bool force_update)
{
    uint32_t device_address = _ina_handle ? _ina_handle->i2c_device_config.device_address : 0;

    if (!_ina_handle || !voltage_mv || bus_num > 2) return STA_C(ESP_ERR_INVALID_ARG, OWNER_PROVIDER_CURRENT_MONITOR_READ, device_address);
    STA_RET_ON_ESP_ERR(ina3221_update_buses_readings(_ina_handle, force_update), OWNER_PROVIDER_CURRENT_MONITOR_READ, _ina_handle->i2c_device_config.device_address);
    *voltage_mv = (uint32_t)_ina_handle->last_readings.bus_voltage[bus_num];
    return STA_OK;
}

status_rep_t sys_pwr_get_current(uint8_t channel_num, uint32_t* current_ma, bool force_update)
{
    uint32_t device_address = _ina_handle ? _ina_handle->i2c_device_config.device_address : 0;

    if (!_ina_handle || !current_ma || channel_num > 2) return STA_C(ESP_ERR_INVALID_ARG, OWNER_PROVIDER_CURRENT_MONITOR_READ, device_address);
    STA_RET_ON_ESP_ERR(ina3221_update_shunts_readings(_ina_handle, force_update), OWNER_PROVIDER_CURRENT_MONITOR_READ, _ina_handle->i2c_device_config.device_address);
    *current_ma = (uint32_t)_ina_handle->last_readings.shunt_current[channel_num];
    return STA_OK;
}
