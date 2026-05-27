#include "provider_current_monitor.h"
#include "ina3221.h"

#define TAG __FILE_NAME__

static ina3221_handle_t _ina_handle = NULL;

void* p_current_monitor_new(uint8_t i2c_addr) {
    _ina_handle = ina3221_new(i2c_addr);
    return _ina_handle;
}

i2c_device_config_t* p_current_monitor_get_i2c_dev_config(void){
    return &(_ina_handle->i2c_device_config);
}
i2c_master_dev_handle_t*p_current_monitor_get_i2c_dev_handle(void){
    return &(_ina_handle->i2c_master_dev_handle);
}
TaskHandle_t p_current_monitor_get_task_handle(void){
    return _ina_handle->driver_task_handle;
}


static status_rep_t _set_alert(uint8_t channel, int32_t current_mA, bool is_critical)
{

    
    if (!_ina_handle) return STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_PROVIDER_CURRENT_MONITOR, 0);
    if (channel > 2) return STA_C(PWR_ERR_INVALID_PARAM, OWNER_PROVIDER_CURRENT_MONITOR, channel);

    ESP_LOGI(TAG, "Setting %s current limit for CH%d: %ld mA", is_critical ? "CRIT" : "WARN", channel, (long)current_mA);
    STA_RET_ON_ESP_ERR(ina3221_set_average(_ina_handle, INA3221_AVG_64), OWNER_PROVIDER_CURRENT_MONITOR, channel);
    if (is_critical) {
        STA_RET_ON_ESP_ERR(ina3221_set_critical_alert(_ina_handle, channel, current_mA), OWNER_PROVIDER_CURRENT_MONITOR, channel);
    } else {
        STA_RET_ON_ESP_ERR(ina3221_set_warning_alert(_ina_handle, channel, current_mA), OWNER_PROVIDER_CURRENT_MONITOR, channel);
    }
    return STA_OK;
}

status_rep_t p_current_monitor_set_warning(uint8_t channel, int32_t current_mA) {
    return _set_alert(channel, current_mA, false);
}

status_rep_t p_current_monitor_set_crit(uint8_t channel, int32_t current_mA) {
    return _set_alert(channel, current_mA, true);
}

status_rep_t p_current_monitor_get_voltage(uint8_t channel, uint32_t* voltage_mv, bool force_update)
{
    if (!_ina_handle) return STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_PROVIDER_CURRENT_MONITOR, 0);
    if(channel > 2 || !voltage_mv) return STA_C(PWR_ERR_INVALID_PARAM, OWNER_PROVIDER_CURRENT_MONITOR, channel);
    STA_RET_ON_ESP_ERR(ina3221_update_buses_readings(_ina_handle, force_update), OWNER_PROVIDER_CURRENT_MONITOR, _ina_handle->i2c_device_config.device_address);
    *voltage_mv = (uint32_t)_ina_handle->last_readings.bus_voltage[channel];
    return STA_OK;
}

status_rep_t p_current_monitor_get_current(uint8_t channel, int32_t* current_ma, bool force_update)
{
    if(!_ina_handle) return STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_PROVIDER_CURRENT_MONITOR, 0);
    if(channel > 2 || !current_ma) return STA_C(PWR_ERR_INVALID_PARAM, OWNER_PROVIDER_CURRENT_MONITOR, channel);
    STA_RET_ON_ESP_ERR(ina3221_update_shunts_readings(_ina_handle, force_update), OWNER_PROVIDER_CURRENT_MONITOR, _ina_handle->i2c_device_config.device_address);
    *current_ma = (int32_t)(_ina_handle->last_readings.shunt_current[channel]);
    return STA_OK;
}

status_rep_t p_current_monitor_add_cb_warning(uint8_t channel, void (*callback)(void*), void* ctx) {
    if (!_ina_handle) return STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_PROVIDER_CURRENT_MONITOR, 0);
    if(channel > 2) return STA_C(PWR_ERR_INVALID_PARAM, OWNER_PROVIDER_CURRENT_MONITOR, channel);
    ina3221_register_user_callback(_ina_handle, callback, ctx, channel, false);
    return STA_OK;
}

/**
 * @brief Register a warning alert callback for a specific channel
 */
status_rep_t p_current_monitor_register_warning_callback(uint8_t channel, void (*callback)(void*), void* ctx) {
    if (!_ina_handle) return STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_PROVIDER_CURRENT_MONITOR, 0);
    if(channel > 2) return STA_C(PWR_ERR_INVALID_PARAM, OWNER_PROVIDER_CURRENT_MONITOR, channel);
    ina3221_register_user_callback(_ina_handle, callback, ctx, channel, false);
    return STA_OK;
}

/**
 * @brief Register a critical alert callback for a specific channel
 */
status_rep_t p_current_monitor_register_critical_callback(uint8_t channel, void (*callback)(void*), void* ctx) {
    if (!_ina_handle) return STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_PROVIDER_CURRENT_MONITOR, 0);
    if(channel > 2) return STA_C(PWR_ERR_INVALID_PARAM, OWNER_PROVIDER_CURRENT_MONITOR, channel);
    ina3221_register_user_callback(_ina_handle, callback, ctx, channel, true);
    return STA_OK;
}

/**
 * @brief Reset current monitor callbacks for all channels
 * Clears all registered warning and critical alert callbacks and resets hardware alert settings
 */
status_rep_t p_current_monitor_reset_callbacks(void) {
    if (!_ina_handle) return STA_C(PWR_ERR_DEVICE_NOT_FOUND, OWNER_PROVIDER_CURRENT_MONITOR, 0);
    
    /* Reset hardware alert settings: disable latches, clear flags */
    STA_RET_ON_ESP_ERR(ina3221_reset_alerts(_ina_handle), OWNER_PROVIDER_CURRENT_MONITOR, 0);
    
    /* Clear all 6 callbacks (3 channels × 2 alert types: warning + critical) */
    for (uint8_t i = 0; i < 6; i++) {
        _ina_handle->user_callback[i] = NULL;
        _ina_handle->user_callback_arg[i] = NULL;
    }
    
    ESP_LOGI(TAG, "Current monitor callbacks and hardware alerts reset");
    return STA_OK;
}