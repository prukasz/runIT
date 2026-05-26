#include "manager_io.h"

#define TAG __FILE_NAME__

static io_port_dispatch_t port_registry[MAX_IO_PORTS] = {0};
static uint8_t next_free_port = 0;
static bool global_io_is_protected = false;

void manager_io_freeze(void){
    for (uint8_t i = 0; i < next_free_port; i++) {
        if (port_registry[i].freeze) {
            port_registry[i].freeze(true);
        }
    }
}

void manager_io_unfreeze(void){
    for (uint8_t i = 0; i < next_free_port; i++) {
        if (port_registry[i].freeze) {
            port_registry[i].freeze(false);
        }
    }
}


status_rep_t manager_io_register_new_port(io_port_dispatch_t *port_dispatch, uint8_t* out_port_id){
    if (next_free_port >= MAX_IO_PORTS) {
        return STA_W(IO_ERR_NO_FREE_PORT, OWNER_IO_MANAGER, 0);
    }
    memcpy(&port_registry[next_free_port], port_dispatch, sizeof(io_port_dispatch_t));

    if (out_port_id != NULL) {
        *out_port_id = next_free_port;
    }
    next_free_port++;
    ESP_LOGI(TAG, "Registered new IO port with ID %d", next_free_port - 1);
    return STA_OK;
}

status_rep_t sys_io_set_global_protection(bool is_enabled){
    global_io_is_protected = is_enabled;
    return STA_OK;
}

/******************System wide GPIO functions ***************************************/
status_rep_t sys_gpio_set_mode(uint8_t port_id, uint8_t pin, uint32_t mode){
    if (port_id >= MAX_IO_PORTS) return STA_W(IO_ERR_PORT_INVALID, OWNER_IO_PORT_CONFIGURE , port_id);
    if (port_registry[port_id].mode_func == NULL) return STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_CONFIGURE , 0);
    if (global_io_is_protected && (1ULL << pin & port_registry[port_id].protected_pins)) return STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_PORT_CONFIGURE, port_registry[port_id].protected_pins);
    status_rep_t result = port_registry[port_id].mode_func(pin, mode);
    if(!STA_IS_OK(result)){
        ESP_LOGW(TAG, "Failed to set pin mode for port %d, pin %d to mode %u", port_id, pin, mode);
        result.details.severity = 1; // Mark as warning
        STA_P(result);
        STA_RP(STA_C(result.e_code, OWNER_IO_PORT_CONFIGURE, pin));
    }
    ESP_LOGI(TAG, "Set pin mode for port %d, pin %d to mode %u", port_id, pin, mode);
    return STA_OK;
}

status_rep_t sys_gpio_set_level(uint8_t port_id, uint64_t pin_mask, bool level){
    if (port_id >= MAX_IO_PORTS) return STA_W(IO_ERR_PORT_INVALID, OWNER_IO_PORT_SET, port_id);
    if (port_registry[port_id].set_func == NULL) return STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_SET, 0);
    if (global_io_is_protected && (pin_mask & port_registry[port_id].protected_pins)) return STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_PORT_SET, port_registry[port_id].protected_pins);
    status_rep_t result = port_registry[port_id].set_func(pin_mask, level);
    if (!STA_IS_OK(result)) {
        ESP_LOGW(TAG, "Failed to set pin level for port %d, pin mask 0x%016llX: e_code=%u, e_owner=%u", port_id, pin_mask, result.e_code, result.e_owner);
        result.details.severity = 1; // Mark as warning
        STA_P(result);
        STA_RP(STA_C(result.e_code, OWNER_IO_PORT_SET, pin_mask));
    }
    return STA_OK;
}

status_rep_t sys_gpio_read_level(uint8_t port_id, uint64_t pin_mask, uint64_t* level){
    if (port_id >= MAX_IO_PORTS) return STA_W(IO_ERR_PORT_INVALID, OWNER_IO_PORT_READ, port_id);
    if (port_registry[port_id].read_func == NULL) return STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_READ, 0);
    status_rep_t result = port_registry[port_id].read_func(pin_mask, level);
    if (!STA_IS_OK(result)) {
        ESP_LOGW(TAG, "Failed to read pin level for port %d, pin mask 0x%016llX: e_code=%u, e_owner=%u", port_id, pin_mask, result.e_code, result.e_owner);
        result.details.severity = 1; // Mark as warning
        STA_P(result);
        STA_RP(STA_C(result.e_code, OWNER_IO_PORT_READ, pin_mask));
    }
    return STA_OK;
}

status_rep_t sys_gpio_toggle(uint8_t port_id, uint64_t pin_mask){
    if (port_id >= MAX_IO_PORTS) return STA_W(IO_ERR_PORT_INVALID, OWNER_IO_PORT_TOGGLE, port_id);
    if (port_registry[port_id].toggle_func == NULL) return STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_TOGGLE, 0);
    if (global_io_is_protected && (pin_mask & port_registry[port_id].protected_pins)) return STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_PORT_TOGGLE, port_registry[port_id].protected_pins);
    status_rep_t result = port_registry[port_id].toggle_func(pin_mask);
    if (!STA_IS_OK(result)) {
        ESP_LOGW(TAG, "Failed to toggle pin for port %d, pin mask 0x%016llX: e_code=%u, e_owner=%u", port_id, pin_mask, result.e_code, result.e_owner);
        result.details.severity = 1; // Mark as warning
        STA_P(result);
        STA_RP(STA_C(result.e_code, OWNER_IO_PORT_TOGGLE, pin_mask));
    }
    return STA_OK;
}

status_rep_t sys_gpio_register_callback(uint8_t port_id, uint8_t pin, uint32_t mode, void (*callback)(void* arg), void* arg){
    if (port_id >= MAX_IO_PORTS) return STA_W(IO_ERR_PORT_INVALID, OWNER_IO_PORT_CALLBACK, port_id);
    if (port_registry[port_id].callback_add_func == NULL) return STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_CALLBACK, 0);
    if (global_io_is_protected && (1ULL << pin & port_registry[port_id].protected_pins)) return STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_PORT_CALLBACK, port_registry[port_id].protected_pins);
    status_rep_t result = port_registry[port_id].callback_add_func(pin, mode, callback, arg);
    if (!STA_IS_OK(result)) {
        ESP_LOGW(TAG, "Failed to register callback for port %d, pin %d", port_id, pin);
        result.details.severity = 1; // Mark as warning
        STA_P(result);
        STA_RP(STA_C(result.e_code, OWNER_IO_PORT_CALLBACK, pin));
    }
    return STA_OK;
} 
/******************System wide GPIO functions ***************************************/

/****************** System wide PWM functions ***************************************/
status_rep_t sys_io_set_pwm_duty(uint8_t port_id, uint64_t pin_mask, uint32_t duty_cycle){
    if (port_id >= MAX_IO_PORTS) return STA_W(IO_ERR_PORT_INVALID, OWNER_IO_MANAGER, 0);
    if (port_registry[port_id].pwm_set_duty_func == NULL) return STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_MANAGER, 0);
    if (global_io_is_protected && (pin_mask & port_registry[port_id].protected_pins)) return STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_MANAGER, 0);
    return port_registry[port_id].pwm_set_duty_func(pin_mask, duty_cycle);
}

status_rep_t sys_io_set_pwm_freq(uint8_t port_id, uint64_t pin_mask, uint32_t freq_hz){
    if (port_id >= MAX_IO_PORTS) return STA_W(IO_ERR_PORT_INVALID, OWNER_IO_MANAGER, 0);
    if (port_registry[port_id].pwm_set_freq_func == NULL) return STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_MANAGER, 0);
    if (global_io_is_protected && (pin_mask & port_registry[port_id].protected_pins)) return STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_MANAGER, 0);
    return port_registry[port_id].pwm_set_freq_func(pin_mask, freq_hz);
}
/****************** System wide PWM functions ***************************************/



/****************** System wide ADC functions ***************************************/
status_rep_t sys_io_adc_read(uint8_t port_id, uint64_t pin_mask, uint32_t* out_mv, uint8_t max_results_num){
    if (port_id >= MAX_IO_PORTS) return STA_W(IO_ERR_PORT_INVALID, OWNER_IO_PORT_READ, port_id);
    if (port_registry[port_id].adc_read_func == NULL) return STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_READ, 0);
    status_rep_t result = port_registry[port_id].adc_read_func(pin_mask, out_mv, max_results_num);
    if (!STA_IS_OK(result)) {
        ESP_LOGW(TAG, "Failed to read ADC for port %d, pin mask 0x%016llX: e_code=%u, e_owner=%u", port_id, pin_mask, result.e_code, result.e_owner);
        result.details.severity = 1; // Mark as warning
        STA_P(result);
        STA_RP(STA_C(result.e_code, OWNER_IO_PORT_READ, pin_mask));
    }
    return STA_OK;
}

status_rep_t sys_io_adc_register_callback(uint8_t port_id, uint8_t pin, void* adc_int_config){
    if (port_id >= MAX_IO_PORTS) return STA_W(IO_ERR_PORT_INVALID, OWNER_IO_PORT_CALLBACK, port_id);
    if (port_registry[port_id].adc_callback_add_func == NULL) return STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_CALLBACK, 0);
    if (global_io_is_protected && (1ULL << pin & port_registry[port_id].protected_pins)) return STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_PORT_CALLBACK, port_registry[port_id].protected_pins);
    return port_registry[port_id].adc_callback_add_func(pin, adc_int_config);
}
/****************** System wide ADC functions ***************************************/



