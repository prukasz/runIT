#include "io_sys_ina_wrapper.h"
#include "esp_log.h"

static const char *TAG = "IO_SYS_INA";

// Local static pointer to hold the driver instance
static ina3221_handle_t s_ina_handle = NULL;

void ina3221_wrapper_init(ina3221_handle_t handle)
{
    if (handle != NULL) {
        s_ina_handle = handle;
        ESP_LOGI(TAG, "Wrapper initialized successfully.");
    } else {
        ESP_LOGE(TAG, "Failed to initialize wrapper: handle is NULL");
    }
}

esp_err_t io_sys_set_max_power_critical(float power_w)
{
    if (!s_ina_handle) return ESP_ERR_INVALID_STATE;

    float bus_v0_mv = 0;
    
    // Force a read of the bus voltage to ensure our calculation is accurate
    esp_err_t err = io_sys_get_bus_voltage(0, &bus_v0_mv, true);
    if (err != ESP_OK) return err;

    // Prevent division by zero if the bus is turned off or shorted
    if (bus_v0_mv <= 0.0f) {
        ESP_LOGE(TAG, "Bus 0 voltage is 0, cannot calculate power limit.");
        return ESP_ERR_INVALID_STATE;
    }

    // P = V * I  -->  I = P / V
    // Convert bus voltage to Volts for the math
    float bus_v0_volts = bus_v0_mv / 1000.0f;
    float limit_amps = power_w / bus_v0_volts;
    float limit_ma = limit_amps * 1000.0f;

    ESP_LOGI(TAG, "Setting CH0 Critical Limit to %.2f mA (based on %.2f W at %.2f V)", limit_ma, power_w, bus_v0_volts);

    return ina3221_set_critical_alert(s_ina_handle, 0, limit_ma);
}

esp_err_t (float power_w)
{
    if (!s_ina_handle) return ESP_ERR_INVALID_STATE;

    esp_err_t err = ESP_OK;
    float bus_mv = 0;

    // --- Channel 0 Warning Setup ---
    if (io_sys_get_bus_voltage(0, &bus_mv, true) == ESP_OK && bus_mv > 0.0f) {
        float limit_ma_0 = (power_w / (bus_mv / 1000.0f)) * 1000.0f;
        err = ina3221_set_warning_alert(s_ina_handle, 0, limit_ma_0);
        if (err != ESP_OK) return err;
    } else {
        ESP_LOGW(TAG, "Skipped CH0 warning setup (Invalid bus voltage)");
    }

    // --- Channel 2 Warning Setup ---
    if (io_sys_get_bus_voltage(2, &bus_mv, true) == ESP_OK && bus_mv > 0.0f) {
        float limit_ma_2 = (power_w / (bus_mv / 1000.0f)) * 1000.0f;
        err = ina3221_set_warning_alert(s_ina_handle, 2, limit_ma_2);
        if (err != ESP_OK) return err;
    } else {
        ESP_LOGW(TAG, "Skipped CH2 warning setup (Invalid bus voltage)");
    }

    return err;
}

esp_err_t io_sys_get_bus_voltage(uint8_t bus_num, float* voltage_mv, bool force_update)
{
    if (!s_ina_handle || !voltage_mv || bus_num > 2) return ESP_ERR_INVALID_ARG;

    if (force_update) {
        // Calling this with 'true' blocks and executes the I2C transaction immediately 
        esp_err_t err = ina3221_update_buses_readings(s_ina_handle, true);
        if (err != ESP_OK) return err;
    } else {
        // If false, flag the task to update it in the background for next time
        ina3221_update_buses_readings(s_ina_handle, false);
    }

    *voltage_mv = s_ina_handle->last_readings.bus_voltage[bus_num];
    return ESP_OK;
}

esp_err_t io_sys_get_shunt_voltage(uint8_t channel_num, float* voltage_mv, bool force_update)
{
    if (!s_ina_handle || !voltage_mv || channel_num > 2) return ESP_ERR_INVALID_ARG;

    if (force_update) {
        esp_err_t err = ina3221_update_shunts_readings(s_ina_handle, true);
        if (err != ESP_OK) return err;
    } else {
        ina3221_update_shunts_readings(s_ina_handle, false);
    }

    *voltage_mv = s_ina_handle->last_readings.shunt_voltage[channel_num];
    return ESP_OK;
}

esp_err_t io_sys_get_current(uint8_t channel_num, float* current_ma, bool force_update)
{
    if (!s_ina_handle || !current_ma || channel_num > 2) return ESP_ERR_INVALID_ARG;

    if (force_update) {
        // Updating shunts automatically recalculates the current in the driver
        esp_err_t err = ina3221_update_shunts_readings(s_ina_handle, true);
        if (err != ESP_OK) return err;
    } else {
        ina3221_update_shunts_readings(s_ina_handle, false);
    }

    *current_ma = s_ina_handle->last_readings.shunt_current[channel_num];
    return ESP_OK;
}