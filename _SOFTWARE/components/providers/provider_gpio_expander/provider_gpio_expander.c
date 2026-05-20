#include "provider_gpio_expander.h"
#include "tca6424a.h"

static tca6424a_handle_t _tca_handle = NULL;

static bool _dereffered_mode = false;

void _sys_expander_gpio_delay_updates(bool dereffered_mode){
    _dereffered_mode = dereffered_mode;
}

status_rep_t _sys_expander_configure_pins(uint64_t pin_mask, uint32_t mode_mask){
    esp_err_t err = tca_preset_cfg(_tca_handle, (uint32_t)pin_mask, mode_mask, !_dereffered_mode);
    if (err != ESP_OK) {
        return STA_C(EXPANDER_UPDATE_FAILED, OWNER_PROVIDER_GPIO_EXPANDER_CONFIGURE_PINS, err);
    }
    return STA_OK;
}

status_rep_t _sys_io_expander_set_pin(uint64_t pin_mask, bool state){
    esp_err_t err = tca_preset_pins(_tca_handle, (uint32_t)pin_mask, state ? (uint32_t)pin_mask : 0, !_dereffered_mode);
    if (err != ESP_OK) {
        return STA_C(EXPANDER_UPDATE_FAILED, OWNER_PROVIDER_GPIO_EXPANDER_SET_PINS, err);
    }
    return STA_OK;
}

status_rep_t _sys_io_expander_read_pins(uint64_t* out_mask){
    uint32_t temp_mask = 0;
    esp_err_t err = tca_get_pin_level(_tca_handle, &temp_mask, !_dereffered_mode);
    if (err != ESP_OK) {
        return STA_C(EXPANDER_UPDATE_FAILED, OWNER_PROVIDER_GPIO_EXPANDER_READ_PINS, err);
    }
    *out_mask = (uint64_t)temp_mask;
    return STA_OK;
}

status_rep_t _sys_io_expander_read_pin(uint64_t pin_mask, bool* out_mask){
    uint32_t level = 0;
    esp_err_t err = tca_get_pin_level(_tca_handle, &level, !_dereffered_mode);
    if (err != ESP_OK) {
        return STA_C(EXPANDER_UPDATE_FAILED, OWNER_PROVIDER_GPIO_EXPANDER_READ_PINS, err);
    }
    *out_mask = (level & (uint32_t)pin_mask) ? true : false;
    return STA_OK;
}

status_rep_t _sys_io_expander_toggle_pin(uint64_t pin_mask){
    uint32_t current_level = tca_get_pin_output(_tca_handle);
    uint32_t new_level = (current_level ^ (uint32_t)pin_mask) & (uint32_t)pin_mask; 
    esp_err_t err = tca_preset_pins(_tca_handle, (uint32_t)pin_mask, new_level, !_dereffered_mode);
    if (err != ESP_OK) {
        return STA_C(EXPANDER_UPDATE_FAILED, OWNER_PROVIDER_GPIO_EXPANDER_TOGGLE_PINS, err);
    }
    return STA_OK;
}


status_rep_t _sys_expander_gpio_set_callback(uint64_t pin_mask, uint32_t mode, void (*callback)(void* arg), void* arg){
    if (mode >= 3) {
        return STA_C(EXPANDER_UNSUPPORTED_MODE, OWNER_PROVIDER_GPIO_EXPANDER_SET_CALLBACK, mode);
    }
    if (tca_register_pin_callback(_tca_handle, (uint32_t)pin_mask, callback, mode, arg) != ESP_OK) {
        return STA_C(EXPANDER_INVALID_PIN, OWNER_PROVIDER_GPIO_EXPANDER_SET_CALLBACK, (uint32_t)pin_mask);
    }
    return STA_OK;
}

void * provider_gpio_expander_new_handle(uint8_t i2c_addr){
    _tca_handle = tca_new_handle(i2c_addr);
    return (void*)_tca_handle;
}


i2c_master_dev_handle_t provider_gpio_expander_get_i2c_dev_handle(){
    return _tca_handle->i2c_dev_handle;
}

TaskHandle_t provider_gpio_expander_get_task_handle(){
    return _tca_handle->task_handle;
}

i2c_device_config_t* provider_gpio_expander_get_i2c_dev_config(){
    return &_tca_handle->i2c_dev_config;
}


