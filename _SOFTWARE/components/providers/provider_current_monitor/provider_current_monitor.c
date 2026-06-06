#include "provider_current_monitor.h"
#include "ina3221.h"

#define TAG __FILE_NAME__
#undef OWNER
#define OWNER OWNER_PROVIDER_CURRENT_MONITOR



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


status_rep_t p_current_monitor_set_warning(uint8_t channel, int32_t current_mA) {
    CHECK_HANDLE_R(_ina_handle);
    ESP_LOGI(TAG, "Setting current limit for CH %d: %ld mA WARN", channel, (long)current_mA);
    CHECK_ESP_CALL_R(ina3221_set_average(_ina_handle, INA3221_AVG_64));
    CHECK_ESP_CALL_R(ina3221_set_alert(_ina_handle, channel, current_mA, false));
    return STA_OK;
}

status_rep_t p_current_monitor_set_crit(uint8_t channel, int32_t current_mA) {
    CHECK_HANDLE_R(_ina_handle);
    ESP_LOGI(TAG, "Setting current limit for CH%d: %ld mA CRIT", channel, (long)current_mA);
    CHECK_ESP_CALL_R(ina3221_set_average(_ina_handle, INA3221_AVG_64));
    CHECK_ESP_CALL_R(ina3221_set_alert(_ina_handle, channel, current_mA, true));
    return STA_OK;
}

status_rep_t p_current_monitor_get_voltage(uint8_t channel, uint32_t* voltage_mv, bool force_update)
{
    CHECK_HANDLE_R(_ina_handle);
    CHECK_ESP_CALL_R(ina3221_update_buses_readings(_ina_handle, force_update));
    *voltage_mv = (uint32_t)_ina_handle->last_readings.bus_voltage[channel];
    return STA_OK;
}

status_rep_t p_current_monitor_get_current(uint8_t channel, int32_t* current_ma, bool force_update)
{
    CHECK_HANDLE_R(_ina_handle);
    CHECK_ESP_CALL_R(ina3221_update_shunts_readings(_ina_handle, force_update));
    *current_ma = (int32_t)(_ina_handle->last_readings.shunt_current[channel]);
    return STA_OK;
}

status_rep_t p_current_monitor_add_cb_warning(uint8_t channel, void (*callback)(void*), void* ctx) {
    CHECK_HANDLE_R(_ina_handle);
    ina3221_register_user_callback(_ina_handle, callback, ctx, channel, false);
    return STA_OK;
}

/**
 * @brief Register a warning alert callback for a specific channel
 */
status_rep_t p_current_monitor_register_warning_callback(uint8_t channel, void (*callback)(void*), void* ctx) {
    CHECK_HANDLE_R(_ina_handle);
    ina3221_register_user_callback(_ina_handle, callback, ctx, channel, false);
    return STA_OK;
}

/**
 * @brief Register a critical alert callback for a specific channel
 */
status_rep_t p_current_monitor_register_critical_callback(uint8_t channel, void (*callback)(void*), void* ctx) {
    CHECK_HANDLE_R(_ina_handle);
    ina3221_register_user_callback(_ina_handle, callback, ctx, channel, true);
    return STA_OK;
}

/**
 * @brief Reset current monitor callbacks for all channels
 * Clears all registered warning and critical alert callbacks and resets hardware alert settings
 */

status_rep_t p_current_monitor_reset(void) {
    CHECK_HANDLE_R(_ina_handle);
 
    /* Reset hardware alert settings: disable latches, clear flags */
    CHECK_ESP_CALL_R(ina3221_reset(_ina_handle));
    CHECK_ESP_CALL_R(ina3221_set_options(_ina_handle, 0, 1,1));
    CHECK_ESP_CALL_R(ina3221_set_options(_ina_handle, 1, 1,1));
    CHECK_ESP_CALL_R(ina3221_set_options(_ina_handle, 2, 1,1));
    CHECK_ESP_CALL_R(ina3221_enable_latch_pin(_ina_handle, 1, 1));
    /* Clear all 6 callbacks (3 channels × 2 alert types: warning + critical) */
    for (uint8_t i = 0; i < 6; i++) {
        _ina_handle->user_callback[i] = NULL;
        _ina_handle->user_callback_arg[i] = NULL;
    }
    
    ESP_LOGI(TAG, "Current monitor callbacks and hardware alerts reset");
    return STA_OK;
}