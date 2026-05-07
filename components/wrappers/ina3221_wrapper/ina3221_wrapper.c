#include "ina3221_wrapper.h"
#include "esp_log.h"


#define TAG __FILE_NAME__

static ina3221_handle_t _ina_handle = NULL;

status_rep_t ina3221_wrapper_init(ina3221_handle_t handle)
{
    _ina_handle = handle;
    if (!_ina_handle) { return STA_C(ERR_ESP_ERR_INVALID_ARG, OWN_ina3221_wrapper_init, 0); }
    
    STA_C_RET_ON_ESP_ERR_PUSH_LOG(ina3221_set_options(_ina_handle, 1, 1 ,1),
    OWN_ina3221_wrapper_init, _ina_handle->i2c_device_config.device_address);
    
    STA_C_RET_ON_ESP_ERR_PUSH_LOG(ina3221_enable_latch_pin(_ina_handle, true, true),
    OWN_ina3221_wrapper_init, _ina_handle->i2c_device_config.device_address);
    STA_C_RET_ON_ESP_ERR_PUSH_LOG(ina3221_set_warning_alert(_ina_handle, 0, 200.0f),
    OWN_ina3221_wrapper_init, _ina_handle->i2c_device_config.device_address);
    STA_C_RET_ON_ESP_ERR_PUSH_LOG(ina3221_set_critical_alert(_ina_handle, 0, 1000.0f),
    OWN_ina3221_wrapper_init, _ina_handle->i2c_device_config.device_address);
    return STA_OK;
}

esp_err_t io_sys_periph_set_ina3221_pwr_crit(uint8_t channel_num, float power_w)
{
    float bus_v0_mv = 0;

    // Force a read of the bus voltage to ensure our calculation is accurate
    esp_err_t err = io_sys_periph_get_ina3221_bus_voltage(0, &bus_v0_mv, true);

    if (err != ESP_OK) return err;

    float bus_v0_volts = bus_v0_mv / 1000.0f;
    float limit_amps = power_w / bus_v0_volts;
    float limit_ma = limit_amps * 1000.0f;
    
    ESP_LOGI(TAG, "Setting CH%d Warning Limit to %.2f mA (based on %.2f W at %.2f V)", channel_num, limit_ma, power_w, bus_v0_volts);
    return ina3221_set_warning_alert(_ina_handle, channel_num, limit_ma);
}

esp_err_t io_sys_periph_ina3221_set_pwr_warning(uint8_t channel_num, float power_w)
{
    float bus_v0_mv = 0;

    // Force a read of the bus voltage to ensure our calculation is accurate
    esp_err_t err = io_sys_periph_get_ina3221_bus_voltage(0, &bus_v0_mv, true);

    if (err != ESP_OK) return err;

    float bus_v0_volts = bus_v0_mv / 1000.0f;
    float limit_amps = power_w / bus_v0_volts;
    float limit_ma = limit_amps * 1000.0f;
    
    ESP_LOGI(TAG, "Setting CH%d Warning Limit to %.2f mA (based on %.2f W at %.2f V)", channel_num, limit_ma, power_w, bus_v0_volts);
    return ina3221_set_warning_alert(_ina_handle, channel_num, limit_ma);
}
    

esp_err_t io_sys_periph_get_ina3221_bus_voltage(uint8_t bus_num, float* voltage_mv, bool force_update)
{
    if (force_update) {
        // Calling this with 'true' blocks and executes the I2C transaction immediately 
        esp_err_t err = ina3221_update_buses_readings(_ina_handle, true);
        if (err != ESP_OK) return err;
    } else {
        // If false, flag the task to update it in the background for next time
        ina3221_update_buses_readings(_ina_handle, false);
    }

    *voltage_mv = _ina_handle->last_readings.bus_voltage[bus_num];
    return ESP_OK;
}

esp_err_t io_sys_periph_ina3221_get_shunt_voltage(uint8_t channel_num, float* voltage_mv, bool force_update)
{
    if (!_ina_handle || !voltage_mv || channel_num > 2) return ESP_ERR_INVALID_ARG;

    if (force_update) {
        esp_err_t err = ina3221_update_shunts_readings(_ina_handle, true);
        if (err != ESP_OK) return err;
    } else {
        ina3221_update_shunts_readings(_ina_handle, false);
    }

    *voltage_mv = _ina_handle->last_readings.shunt_voltage[channel_num];
    return ESP_OK;
}

esp_err_t io_sys_periph_ina3221_get_current(uint8_t channel_num, float* current_ma, bool force_update)
{
    if (!_ina_handle || !current_ma || channel_num > 2) return ESP_ERR_INVALID_ARG;

    if (force_update) {
        // Updating shunts automatically recalculates the current in the driver
        esp_err_t err = ina3221_update_shunts_readings(_ina_handle, true);
        if (err != ESP_OK) return err;
    } else {
        ina3221_update_shunts_readings(_ina_handle, false);
    }

    *current_ma = _ina_handle->last_readings.shunt_current[channel_num];
    return ESP_OK;
}