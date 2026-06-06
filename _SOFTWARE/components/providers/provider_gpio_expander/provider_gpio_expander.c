#include "provider_gpio_expander.h"
#include "tca6424a.h"
#include "manager_io.h"

#define TAG __FILE_NAME__
#undef OWNER
#define OWNER OWNER_PROVIDER_GPIO_EXPANDER


static tca6424a_handle_t _tca_handle = NULL;

static bool _freeze = false;
static uint8_t my_port_id = 0xFF;  // Port ID assigned by IO manager
static uint32_t configured_pins = 0;

void p_gpio_expander_freeze(bool freeze){
    _freeze = freeze;
}

status_rep_t p_gpio_expander_configure_pins(uint8_t pin, uint32_t mode) {
    CHECK_HANDLE_R(_tca_handle);
    CHECK_ARG_R(pin, 0, 23, SYS_IO_MAKE_INFO(my_port_id, pin, mode));

    if (configured_pins & (1UL << pin)) {
        return STA_C(IO_ERR_PIN_IN_OTHER_USE, OWNER_PROVIDER_GPIO_EXPANDER, SYS_IO_MAKE_INFO(my_port_id, pin, mode));
    }

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
            return STA_C(IO_ERR_MODE_UNSUPPORTED, OWNER_PROVIDER_GPIO_EXPANDER, SYS_IO_MAKE_INFO(my_port_id, pin, mode));
    }

    CHECK_ESP_CALL_R(tca_preset_cfg(_tca_handle, 1UL << pin, tca_cfg_state));
    configured_pins |= (1UL << pin);
    return STA_OK;
}

status_rep_t p_gpio_expander_set_pins(uint64_t pin_mask, bool state){
    CHECK_HANDLE_R(_tca_handle);
    CHECK_ESP_CALL_R(tca_set_pins(_tca_handle, (uint32_t)pin_mask, state ? (uint32_t)pin_mask : 0, !_freeze));

    return STA_OK;
}

status_rep_t p_gpio_expander_read_pins(uint64_t* out_mask){
    CHECK_HANDLE_R(_tca_handle);
    uint32_t temp_mask = 0;
    CHECK_ESP_CALL_R(tca_get_pins(_tca_handle, &temp_mask, !_freeze));
    
    *out_mask = (uint64_t)temp_mask;
    return STA_OK;
}

status_rep_t p_gpio_epander_read_pin(uint64_t pin_mask, uint64_t* out_mask){
    CHECK_HANDLE_R(_tca_handle);
    uint32_t level = 0;
    CHECK_ESP_CALL_R(tca_get_pins(_tca_handle, &level, !_freeze));
    
    *out_mask = level & (uint32_t)pin_mask;
    return STA_OK;
}

status_rep_t p_gpio_expander_toggle_pin(uint64_t pin_mask){
    CHECK_HANDLE_R(_tca_handle);
    uint32_t current_level = tca_get_pin_output(_tca_handle);
    uint32_t new_level = (current_level ^ (uint32_t)pin_mask) & (uint32_t)pin_mask; 
    CHECK_ESP_CALL_R(tca_set_pins(_tca_handle, (uint32_t)pin_mask, new_level, !_freeze));
    
    return STA_OK;
}

status_rep_t p_gpio_expander_reset_pin(uint8_t pin){
    CHECK_HANDLE_R(_tca_handle);
    CHECK_ARG_R(pin, 0, 23, SYS_IO_MAKE_INFO(my_port_id, pin, 0)); 

    _tca_handle->callbacks[pin] = NULL;
    _tca_handle->callback_args[pin] = NULL;
    _tca_handle->pin_trigger_modes[pin] = 0;
    configured_pins &= ~(1UL << pin);

    CHECK_ESP_CALL_R(tca_set_pins(_tca_handle, 1UL << pin, 0, !_freeze));
    CHECK_ESP_CALL_R(tca_preset_cfg(_tca_handle, 1UL << pin, 1UL << pin));
  
    return STA_OK;
}


status_rep_t p_gpio_expander_set_pin_callback(uint8_t pin, uint32_t mode, void (*callback)(void* arg), void* arg){
    CHECK_HANDLE_R(_tca_handle);
    CHECK_ARG_R(pin, 0, 24, SYS_IO_MAKE_INFO(my_port_id, pin, 0)); 
    if (mode >= 3) {
        return STA_C(IO_ERR_MODE_UNSUPPORTED, OWNER_PROVIDER_GPIO_EXPANDER, SYS_IO_MAKE_INFO(my_port_id, pin, mode));
    }
    CHECK_ESP_CALL_R(tca_register_pin_callback(_tca_handle, 1UL << pin, callback, mode, arg) != ESP_OK);
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

void p_gpio_expander_set_port_id(uint8_t port_id) {
    my_port_id = port_id;
    ESP_LOGI(TAG, "GPIO expander provider port ID set to %d", port_id);
}



