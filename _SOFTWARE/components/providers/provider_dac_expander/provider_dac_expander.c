#include "provider_dac_expander.h"
#include "esp_log.h"

#define TAG __FILE_NAME__

#undef OWNER
#define OWNER OWNER_PROVIDER_DAC_EXPANDER

static dac53202_handle_t _dac_handle = NULL;
static bool _freeze = false;

void* p_dac_expander_new(uint8_t i2c_addr) {
    _dac_handle = dac53202_new(i2c_addr);
    return (void*)_dac_handle;
}

i2c_device_config_t* p_dac_expander_get_i2c_dev_config(void) {
    return &(_dac_handle->i2c_device_config);
}

i2c_master_dev_handle_t* p_dac_expander_get_i2c_dev_handle(void) {
    return &(_dac_handle->i2c_dev_handle);
}

TaskHandle_t p_dac_expander_get_task_handle(void) {
    return _dac_handle->task_handle;
}

void p_dac_expander_freeze(bool freeze) {
    _freeze = freeze;
}

status_rep_t p_dac_expander_configure(void) {
    CHECK_HANDLE_R(_dac_handle);
    esp_err_t err = dac53202_preset_cfg(_dac_handle, 0x03, 0x03, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure DAC53202");
        return STA_C(PWR_ERR_UPDATE_FAILED, OWNER_PROVIDER_DAC_EXPANDER, err);
    }
    return STA_OK;
}

status_rep_t p_dac_expander_set_channel_mv(uint8_t channel, uint32_t mv) {
    CHECK_HANDLE_R(_dac_handle);
    if (channel > 1) return STA_C(ERR_INVALID_ARG, OWNER_PROVIDER_DAC_EXPANDER, channel);

    uint8_t channel_mask = (1 << channel);
    esp_err_t err = dac53202_set_voltage_mv(_dac_handle, channel_mask, mv, !_freeze);
    if (err != ESP_OK) {
        return STA_C(PWR_ERR_UPDATE_FAILED, OWNER_PROVIDER_DAC_EXPANDER, err);
    }
    return STA_OK;
}

status_rep_t p_dac_expander_get_channel_mv(uint8_t channel, uint32_t *mv) {
    CHECK_HANDLE_R(_dac_handle);
    if (!mv || channel > 1) return STA_C(ERR_INVALID_ARG, OWNER_PROVIDER_DAC_EXPANDER, channel);

    uint16_t v_mv = 0;
    esp_err_t err = dac53202_get_voltage_mv(_dac_handle, channel, &v_mv);
    if (err != ESP_OK) {
        return STA_C(PWR_ERR_UPDATE_FAILED, OWNER_PROVIDER_DAC_EXPANDER, err);
    }
    *mv = v_mv;
    return STA_OK;
}

status_rep_t p_dac_expander_reset(void) {
    CHECK_HANDLE_R(_dac_handle);
    esp_err_t err = dac53202_preset_cfg(_dac_handle, 0x03, 0x00, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to reset DAC53202");
        return STA_C(PWR_ERR_UPDATE_FAILED, OWNER_PROVIDER_DAC_EXPANDER, err);
    }
    ESP_LOGI(TAG, "DAC provider reset: channels set to Hi-Z");
    return STA_OK;
}
