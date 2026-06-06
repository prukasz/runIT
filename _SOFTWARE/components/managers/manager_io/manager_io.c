#include "manager_io.h"
#include "rtos_utils.h"


#define TAG __FILE_NAME__

const char* const io_mode_string[] = {
    "INPUT",
    "OUTPUT_PUSH_PULL",
    "OUTPUT_OPEN_DRAIN",
    "INPUT_PULLUP",
    "INPUT_PULLDOWN",
    "PWM",
    "ADC"
};

const char* const io_feature_string[] = {
    "MODE",
    "SET",
    "READ",
    "TOGGLE",
    "RESET_PIN",
    "CALLBACK_ADD",
    "PWM_SET_DUTY",
    "PWM_SET_FREQ",
    "ADC_READ",
    "ADC_REGISTER_CALLBACK",
    "FREEZE",
    "RESET"
};

const char* sys_io_mode_to_string(sys_gpio_mode_e mode){
    if(mode <= SYS_GPIO_MODE_ADC) return io_mode_string[mode];
    return "UNKNOWN";
}

const char* sys_io_feature_to_string(sys_io_feature_e feature){
    if(feature <= IO_FEATURE_RESET) return io_feature_string[feature];
    return "UNKNOWN";
}


static io_port_dispatch_t port_registry[MAX_IO_PORTS] = {0};
static uint8_t next_free_port = 0;
static bool global_io_is_protected = false;

void manager_io_freeze(bool yes_or_no){
    for (uint8_t i = 0; i < next_free_port; i++) {
        if (port_registry[i].freeze) {
            port_registry[i].freeze(yes_or_no);
        }
    }
}


#undef OWNER
#define OWNER OWNER_IO_MANAGER
status_rep_t manager_io_register_new_port(io_port_dispatch_t *port_dispatch, uint8_t* out_port_id){
    CHECK_NOT_NULL_R(port_dispatch);
    CHECK_NOT_NULL_R(out_port_id);
    if (next_free_port >= MAX_IO_PORTS) {
        STA_RP(STA_W(IO_ERR_NO_FREE_PORT, OWNER_IO_MANAGER, 0));
    }
    memcpy(&port_registry[next_free_port], port_dispatch, sizeof(io_port_dispatch_t));

    if (out_port_id != NULL) {
        *out_port_id = next_free_port;
    }
    next_free_port++;
    ESP_LOGI(TAG, "Registered new IO port with ID %d", next_free_port - 1);
    return STA_OK;
}

status_rep_t sys_io_enable_global_protection(bool is_enabled){
    global_io_is_protected = is_enabled;
    return STA_OK;
}

void sys_io_set_protected_pins(uint8_t port_id, uint64_t pin_mask){
    if (port_id > MAX_IO_PORTS) return;
    port_registry[port_id].protected_pins = pin_mask;
}

/******************System wide GPIO functions ***************************************/
#undef OWNER
#define OWNER OWNER_IO_PORT_CONFIGURE
status_rep_t sys_gpio_set_mode(uint8_t port_id, uint8_t pin, uint32_t mode){
    if (port_registry[port_id].mode_func == NULL) STA_RP(STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_CONFIGURE , SYS_IO_MAKE_INFO(port_id, pin, mode)));
    CHECK_ARG_RP(port_id, 0, MAX_IO_PORTS, SYS_IO_MAKE_INFO(port_id, pin, mode));
    CHECK_ARG_RP(pin, 0, 63, SYS_IO_MAKE_INFO(port_id, pin, mode));
    if (global_io_is_protected && (1ULL << pin & port_registry[port_id].protected_pins)) STA_RP(STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_PORT_CONFIGURE, SYS_IO_MAKE_INFO(port_id, pin, mode)));
    status_rep_t result = port_registry[port_id].mode_func(pin, mode);
    if(STA_P_ON_ESP_ERR(result)){
        ESP_LOGW(TAG, "Failed to set pin mode for port %d, pin %d", port_id, pin);
        STA_RP(STA_C(IO_ERR_UPDATE_FAILED, OWNER_IO_PORT_CONFIGURE, SYS_IO_MAKE_INFO(port_id, pin, mode)));
    }
    STA_RP_ON_ERR(result);
    ESP_LOGI(TAG, "Pin mode set successfully for port %d, pin %d", port_id, pin);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_IO_PORT_SET
status_rep_t sys_gpio_set_level(uint8_t port_id, uint64_t pin_mask, bool level){
    CHECK_ARG_RP(port_id, 0, MAX_IO_PORTS, SYS_IO_MAKE_INFO(port_id, 0, 0));
    if (port_registry[port_id].set_func == NULL) STA_RP(STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_SET, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_SET)));
    if (global_io_is_protected && (pin_mask & port_registry[port_id].protected_pins)) STA_RP(STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_PORT_SET, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_SET)));
    status_rep_t result = port_registry[port_id].set_func(pin_mask, level);
    if (STA_P_ON_ESP_ERR(result)) {
        ESP_LOGW(TAG, "Failed to set pin level for port %d, pin mask 0x%016llX", port_id, pin_mask);
        STA_RP(STA_C(IO_ERR_UPDATE_FAILED, OWNER_IO_PORT_SET, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_SET)));
    }
    STA_RP_ON_ERR(result);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_IO_PORT_READ
status_rep_t sys_gpio_read_level(uint8_t port_id, uint64_t pin_mask, uint64_t* level){
    CHECK_ARG_RP(port_id, 0, MAX_IO_PORTS, SYS_IO_MAKE_INFO(port_id, 0, 0));
    CHECK_NOT_NULL_R(level);
    if (port_registry[port_id].read_func == NULL) STA_RP(STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_READ, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_READ)));
    status_rep_t result = port_registry[port_id].read_func(pin_mask, level);
    if (STA_P_ON_ESP_ERR(result)) {
        ESP_LOGW(TAG, "Failed to read pin level for port %d, pin mask 0x%016llX", port_id, pin_mask);
        STA_RP(STA_C(IO_ERR_UPDATE_FAILED, OWNER_IO_PORT_READ, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_READ)));
    }
    STA_RP_ON_ERR(result);
    return STA_OK;
}
#undef OWNER
#define OWNER OWNER_IO_PORT_TOGGLE
status_rep_t sys_gpio_toggle(uint8_t port_id, uint64_t pin_mask){
    CHECK_ARG_RP(port_id, 0, MAX_IO_PORTS, SYS_IO_MAKE_INFO(port_id, 0, 0));
    if (port_registry[port_id].toggle_func == NULL) STA_RP(STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_TOGGLE, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_TOGGLE)));
     if (global_io_is_protected && (pin_mask & port_registry[port_id].protected_pins)) STA_RP(STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_PORT_TOGGLE, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_TOGGLE)));
    status_rep_t result = port_registry[port_id].toggle_func(pin_mask);
    if (STA_P_ON_ESP_ERR(result)) {
        ESP_LOGW(TAG, "Failed to toggle pin for port %d, pin mask 0x%016llX", port_id, pin_mask);
        STA_RP(STA_C(IO_ERR_UPDATE_FAILED, OWNER_IO_PORT_TOGGLE, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_TOGGLE)));
    }
    STA_RP_ON_ERR(result);
    return STA_OK;
}
#undef OWNER
#define OWNER OWNER_IO_PORT_CONFIGURE
status_rep_t sys_gpio_reset_pin(uint8_t port_id, uint8_t pin){
    CHECK_ARG_RP(port_id, 0, MAX_IO_PORTS, SYS_IO_MAKE_INFO(port_id, pin, 0));
    CHECK_ARG_RP(pin, 0, 63, SYS_IO_MAKE_INFO(port_id, pin, 0));
    uint64_t pin_mask = (1ULL << pin);
    if (port_id >= MAX_IO_PORTS) STA_RP(STA_W(IO_ERR_PORT_INVALID, OWNER_IO_PORT_CONFIGURE, SYS_IO_MAKE_INFO(port_id, pin, IO_FEATURE_RESET)));
    if (port_registry[port_id].reset_pin_func == NULL) STA_RP(STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_CONFIGURE, 0));
    if (global_io_is_protected && (pin_mask & port_registry[port_id].protected_pins)) STA_RP(STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_PORT_CONFIGURE, SYS_IO_MAKE_INFO(port_id, pin, IO_FEATURE_RESET)));

    status_rep_t result = port_registry[port_id].reset_pin_func(pin);
    if (STA_P_ON_ESP_ERR(result)) {
        ESP_LOGW(TAG, "Failed to reset pin for port %d, pin %d", port_id, pin);
        STA_RP(STA_C(IO_ERR_UPDATE_FAILED, OWNER_IO_PORT_CONFIGURE, SYS_IO_MAKE_INFO(port_id, pin, IO_FEATURE_RESET)));
    } 
    STA_RP_ON_ERR(result);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_IO_PORT_CALLBACK
status_rep_t sys_gpio_register_callback(uint8_t port_id, uint8_t pin, uint32_t mode, void (*callback)(void* arg), void* arg){
    CHECK_ARG_RP(port_id, 0, MAX_IO_PORTS, SYS_IO_MAKE_INFO(port_id, pin, 0));
    CHECK_ARG_RP(pin, 0, 63, SYS_IO_MAKE_INFO(port_id, pin, 0));
    if (port_registry[port_id].callback_add_func == NULL) STA_RP(STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_CALLBACK, SYS_IO_MAKE_INFO(port_id, pin, mode)));
    if (global_io_is_protected && (1ULL << pin & port_registry[port_id].protected_pins)) STA_RP(STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_PORT_CALLBACK, SYS_IO_MAKE_INFO(port_id, pin, mode)));
    status_rep_t result = port_registry[port_id].callback_add_func(pin, mode, callback, arg);
    if (STA_P_ON_ESP_ERR(result)) {
            ESP_LOGW(TAG, "Failed to register callback for port %d, pin %d", port_id, pin);
            STA_RP(STA_C(IO_ERR_UPDATE_FAILED, OWNER_IO_PORT_CALLBACK, SYS_IO_MAKE_INFO(port_id, pin, mode)));
    }
    STA_RP_ON_ERR(result);
    return STA_OK;
} 
/******************System wide GPIO functions ***************************************/

/****************** System wide PWM functions ***************************************/
#undef OWNER
#define OWNER OWNER_IO_PORT_SET
status_rep_t sys_io_set_pwm_duty(uint8_t port_id, uint64_t pin_mask, uint32_t duty_cycle){
    CHECK_ARG_RP(port_id, 0, MAX_IO_PORTS, SYS_IO_MAKE_INFO(port_id, 0, 0));

    if (port_registry[port_id].pwm_set_duty_func == NULL) STA_RP(STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_MANAGER, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_PWM_SET_DUTY)));
    if (global_io_is_protected && (pin_mask & port_registry[port_id].protected_pins)) STA_RP(STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_MANAGER, 0));
    status_rep_t result = port_registry[port_id].pwm_set_duty_func(pin_mask, duty_cycle);
    
    if (STA_P_ON_ESP_ERR(result)) {
        ESP_LOGW(TAG, "Failed to set PWM duty cycle for port %d, pin mask 0x%016llX", port_id, pin_mask);
        STA_RP(STA_C(IO_ERR_UPDATE_FAILED, OWNER_IO_MANAGER, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_PWM_SET_DUTY)));
    }
    STA_RP_ON_ERR(result);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_IO_PORT_CONFIGURE
status_rep_t sys_io_set_pwm_freq(uint8_t port_id, uint64_t pin_mask, uint32_t freq_hz){
    CHECK_ARG_RP(port_id, 0, MAX_IO_PORTS, SYS_IO_MAKE_INFO(port_id, 0, 0));
    if (port_registry[port_id].pwm_set_freq_func == NULL) STA_RP(STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_MANAGER, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_PWM_SET_FREQ)));
    if (global_io_is_protected && (pin_mask & port_registry[port_id].protected_pins)) STA_RP(STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_MANAGER, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_PWM_SET_FREQ)));
    status_rep_t result = port_registry[port_id].pwm_set_freq_func(pin_mask, freq_hz);
    if (STA_P_ON_ESP_ERR(result)) {
        ESP_LOGW(TAG, "Failed to set PWM frequency for port %d, pin mask 0x%016llX", port_id, pin_mask);
        STA_RP(STA_C(IO_ERR_UPDATE_FAILED, OWNER_IO_MANAGER, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_PWM_SET_FREQ)));
    }
    STA_RP_ON_ERR(result);
    return STA_OK;
}
/****************** System wide PWM functions ***************************************/



/****************** System wide ADC functions ***************************************/
#undef OWNER
#define OWNER OWNER_IO_PORT_READ
status_rep_t sys_io_adc_read(uint8_t port_id, uint64_t pin_mask, uint32_t* out_mv, uint8_t max_results_num){
    CHECK_ARG_RP(port_id, 0, MAX_IO_PORTS, SYS_IO_MAKE_INFO(port_id, 0, 0));
    CHECK_NOT_NULL_R(out_mv);
    if (port_registry[port_id].adc_read_func == NULL) STA_RP(STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_READ, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_ADC_READ)));
     if (global_io_is_protected && (pin_mask & port_registry[port_id].protected_pins)) STA_RP(STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_PORT_READ, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_ADC_READ)));
    status_rep_t result = port_registry[port_id].adc_read_func(pin_mask, out_mv, max_results_num);
    if (STA_P_ON_ESP_ERR(result)) {
        ESP_LOGW(TAG, "Failed to read ADC value for port %d, pin mask 0x%016llX", port_id, pin_mask);
        STA_RP(STA_C(IO_ERR_UPDATE_FAILED, OWNER_IO_PORT_READ, SYS_IO_MAKE_INFO(port_id, 0, IO_FEATURE_ADC_READ)));
    }
    STA_RP_ON_ERR(result);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_IO_PORT_CALLBACK
status_rep_t sys_io_adc_register_callback(uint8_t port_id, uint8_t pin, void* adc_int_config){
    CHECK_ARG_RP(port_id, 0, MAX_IO_PORTS, SYS_IO_MAKE_INFO(port_id, pin, 0));
    CHECK_ARG_RP(pin, 0, 63, SYS_IO_MAKE_INFO(port_id, pin, 0));
    if (port_registry[port_id].adc_callback_add_func == NULL) STA_RP(STA_W(IO_ERR_FEATURE_UNSUPPORTED, OWNER_IO_PORT_CALLBACK, SYS_IO_MAKE_INFO(port_id, pin, IO_FEATURE_ADC_REGISTER_CALLBACK)));
    if (global_io_is_protected && (1ULL << pin & port_registry[port_id].protected_pins))  STA_RP(STA_W(IO_ERR_PIN_PROTECTED, OWNER_IO_PORT_CALLBACK, SYS_IO_MAKE_INFO(port_id, pin, IO_FEATURE_ADC_REGISTER_CALLBACK)));
    status_rep_t result =  port_registry[port_id].adc_callback_add_func(pin, adc_int_config);
    if (STA_P_ON_ESP_ERR(result)) {
        ESP_LOGW(TAG, "Failed to register ADC callback for port %d, pin %d", port_id, pin);
        STA_RP(STA_C(IO_ERR_UPDATE_FAILED, OWNER_IO_PORT_CALLBACK, SYS_IO_MAKE_INFO(port_id, pin, IO_FEATURE_ADC_REGISTER_CALLBACK)));
    }
    STA_RP_ON_ERR(result);
    return STA_OK;
}
/****************** System wide ADC functions ***************************************/

status_rep_t sys_io_reset_all(void) {
    for (uint8_t i = 0; i < next_free_port; i++) {
        if (port_registry[i].reset) {
            status_rep_t result = port_registry[i].reset();
            if (STA_IS_ERR(result)) {
                ESP_LOGW(TAG, "Failed to reset IO port %d: e_code=%u, e_owner=%u", i, result.e_code, result.e_owner);
                result.details.severity = 1; // Mark as warning
                STA_P(result);
            } else {
                ESP_LOGI(TAG, "Reset IO port %d successfully", i);
            }
        }
    }
    return STA_OK;
}




