#include "provider_gpio_expander.h"
#include "tca6424a.h"
#include "manager_io.h"

static tca6424a_handle_t _tca_handle = NULL;

static bool freeze = false;

#define CHECK_HANDLE(VAL, ret_val) do { if (!(VAL)) return STA_C(IO_ERR_DEVICE_NOT_FOUND, OWNER_PROVIDER_CURRENT_MONITOR, (ret_val)); } while (0)

void p_gpio_expander_freeze(bool freeze){
    freeze = freeze;
}

status_rep_t p_gpio_expander_configure_pins(uint8_t pin, uint32_t mode) {
    CHECK_HANDLE(_tca_handle, 0);
    uint32_t tca_cfg_state = 0; // Default state container

    // Translate the generic sys_gpio_mode_e to TCA6424A hardware logic
    switch (mode) {
        case SYS_GPIO_MODE_OUTPUT_PUSH_PULL:
            tca_cfg_state = 0x00000000; 
            break;

        case SYS_GPIO_MODE_INPUT:
            tca_cfg_state = 0xFFFFFFFF; 
            break;

        default:
            return STA_C(IO_ERR_MODE_UNSUPPORTED, OWNER_PROVIDER_GPIO_EXPANDER, mode);
    }

    // Pass the translated tca_cfg_state (0s or 1s) to the driver.
    // tca_preset_cfg will use the pin_mask to apply this state only to the targeted pins.
    esp_err_t err = tca_preset_cfg(_tca_handle, 1UL << pin, tca_cfg_state);
    
    if (err != ESP_OK) {
        return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_EXPANDER, err);
    }
    return STA_OK;
}

status_rep_t p_gpio_expander_set_pins(uint64_t pin_mask, bool state){
    CHECK_HANDLE(_tca_handle, 0);
    esp_err_t err = tca_set_pins(_tca_handle, (uint32_t)pin_mask, state ? (uint32_t)pin_mask : 0, !freeze);
    if (err != ESP_OK) {
        return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_EXPANDER, err);
    }
    return STA_OK;
}

status_rep_t p_gpio_expander_read_pins(uint64_t* out_mask){
    CHECK_HANDLE(_tca_handle, 0);
    uint32_t temp_mask = 0;
    esp_err_t err = tca_get_pins(_tca_handle, &temp_mask, !freeze);
    if (err != ESP_OK) {
        return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_EXPANDER, err);
    }
    *out_mask = (uint64_t)temp_mask;
    return STA_OK;
}

status_rep_t p_gpio_epander_read_pin(uint64_t pin_mask, uint64_t* out_mask){
    CHECK_HANDLE(_tca_handle, 0);
    uint32_t level = 0;
    esp_err_t err = tca_get_pins(_tca_handle, &level, !freeze);
    if (err != ESP_OK) {
        return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_EXPANDER, err);
    }
    *out_mask = level & (uint32_t)pin_mask;
    return STA_OK;
}

status_rep_t p_gpio_expander_toggle_pin(uint64_t pin_mask){
    CHECK_HANDLE(_tca_handle, 0);
    uint32_t current_level = tca_get_pin_output(_tca_handle);
    uint32_t new_level = (current_level ^ (uint32_t)pin_mask) & (uint32_t)pin_mask; 
    esp_err_t err = tca_set_pins(_tca_handle, (uint32_t)pin_mask, new_level, !freeze);
    if (err != ESP_OK) {
        return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_EXPANDER, err);
    }
    return STA_OK;
}

status_rep_t p_gpio_expander_reset_pin(uint8_t pin){
    CHECK_HANDLE(_tca_handle, 0);
    if (pin >= 24) {
        return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_EXPANDER, pin);
    }

    _tca_handle->callbacks[pin] = NULL;
    _tca_handle->callback_args[pin] = NULL;
    _tca_handle->pin_trigger_modes[pin] = 0;

    esp_err_t err = tca_set_pins(_tca_handle, 1UL << pin, 0, !freeze);
    if (err != ESP_OK) {
        return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_EXPANDER, err);
    }

    err = tca_preset_cfg(_tca_handle, 1UL << pin, 1UL << pin);
    if (err != ESP_OK) {
        return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_EXPANDER, err);
    }

    return STA_OK;
}


status_rep_t p_gpio_expander_set_pin_callback(uint8_t pin, uint32_t mode, void (*callback)(void* arg), void* arg){
    CHECK_HANDLE(_tca_handle, 0);
    if (mode >= 3) {
        return STA_C(IO_ERR_MODE_UNSUPPORTED, OWNER_PROVIDER_GPIO_EXPANDER, mode);
    }
    if (tca_register_pin_callback(_tca_handle, 1UL << pin, callback, mode, arg) != ESP_OK) {
        return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_EXPANDER, pin);
    }
    return STA_OK;
}

void * p_gpio_expander_new(uint8_t i2c_addr){
    _tca_handle = tca_new(i2c_addr);
    return (void*)_tca_handle;
}


i2c_master_dev_handle_t* p_gpio_expander_get_i2c_dev_handle(){
    return &_tca_handle->i2c_dev_handle;
}

TaskHandle_t p_gpio_expander_get_task_handle(){
    return _tca_handle->task_handle;
}

i2c_device_config_t* p_gpio_expander_get_i2c_dev_config(){
    return &_tca_handle->i2c_dev_config;
}



