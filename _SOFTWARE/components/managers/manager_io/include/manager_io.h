#pragma once 
#include "status.h"

#define MAX_IO_PORTS 8

typedef enum{
    SYS_GPIO_MODE_INPUT = 0,
    SYS_GPIO_MODE_OUTPUT_PUSH_PULL = 1,
    SYS_GPIO_MODE_OUTPUT_OPEN_DRAIN = 2,
    SYS_GPIO_MODE_INPUT_PULLUP = 3,
    SYS_GPIO_MODE_INPUT_PULLDOWN = 4,
    SYS_GPIO_MODE_PWM = 5,
    SYS_GPIO_MODE_ADC = 6
}sys_gpio_mode_e;
extern const char* const io_mode_string[];

typedef enum{
    SYS_GPIO_INTR_MODE_RISING_EDGE = 0,
    SYS_GPIO_INTR_MODE_FALLING_EDGE = 1,
    SYS_GPIO_INTR_MODE_BOTH_EDGES = 2,
    SYS_GPIO_INTR_MODE_LEVEL_HIGH = 3,
    SYS_GPIO_INTR_MODE_LEVEL_LOW = 4
}sys_gpio_intr_mode_e;

typedef enum{
    SYS_GPIO_ADC_WINDOW_OUTSIDE = 0,
    SYS_GPIO_ADC_WINDOW_INSIDE = 1,
}sys_gpio_adc_intr_mode_e;

typedef enum{
    IO_FEATURE_MODE = 0,
    IO_FEATURE_SET,
    IO_FEATURE_READ,
    IO_FEATURE_TOGGLE,
    IO_FEATURE_RESET_PIN,
    IO_FEATURE_CALLBACK_ADD,
    IO_FEATURE_PWM_SET_DUTY,
    IO_FEATURE_PWM_SET_FREQ,
    IO_FEATURE_ADC_READ,
    IO_FEATURE_ADC_REGISTER_CALLBACK,
    IO_FEATURE_FREEZE,
    IO_FEATURE_RESET
}sys_io_feature_e;
extern const char* const io_feature_string[];

const char* sys_io_mode_to_string(sys_gpio_mode_e mode);
const char* sys_io_feature_to_string(sys_io_feature_e feature);

typedef struct{
    uint32_t adc_threshold_up_mv;
    uint32_t adc_threshold_down_mv;
    uint32_t adc_threshold_hysteresis_mv;
    uint32_t adc_event_counter_threshold;  
    uint32_t adc_window_mode; //0: outside window, 1: inside window
    void (*callback)(void* arg);
    void* arg;
}sys_io_adc_int_config_t;

typedef uint16_t sys_pin_t;

typedef status_rep_t (*gpio_func_mode)(uint8_t pin, uint32_t mode);
typedef status_rep_t (*gpio_func_set)(uint64_t pins_mask, bool state);
typedef status_rep_t (*gpio_func_read)(uint64_t pins_mask, uint64_t* out_levels);
typedef status_rep_t (*gpio_func_toggle)(uint64_t pins_mask);
typedef status_rep_t (*gpio_func_reset_pin)(uint8_t pin);
typedef status_rep_t (*gpio_func_callback_add)(uint8_t pin, uint32_t mode, void (*callback)(void* arg), void* arg);
typedef status_rep_t (*io_func_pwm_set_duty)(uint64_t pin_mask, uint32_t duty_cycle);
typedef status_rep_t (*io_func_pwm_set_freq)(uint64_t pin_mask, uint32_t freq_hz);
typedef status_rep_t (*io_func_adc_read)(uint64_t pin_mask, uint32_t* out_mv, uint8_t max_results_num);
typedef status_rep_t (*io_func_adc_register_callback)(uint8_t pin, void* adc_int_config);
typedef void (*io_driver_freeze_updates)(bool yes_or_no);
typedef status_rep_t (*io_driver_reset)(void);

typedef struct {
    gpio_func_mode mode_func;
    gpio_func_set set_func;
    gpio_func_read read_func;
    gpio_func_toggle toggle_func;
    gpio_func_reset_pin reset_pin_func;
    gpio_func_callback_add callback_add_func;
    io_func_pwm_set_duty pwm_set_duty_func;
    io_func_pwm_set_freq pwm_set_freq_func;
    io_func_adc_read adc_read_func;
    io_func_adc_register_callback adc_callback_add_func;
    io_driver_freeze_updates freeze;
    io_driver_reset reset;
    uint64_t protected_pins;
} io_port_dispatch_t;


void manager_io_freeze(bool yes_or_no);
void sys_io_set_protected_pins(uint8_t port_id, uint64_t pin_mask);

status_rep_t manager_io_register_new_port(io_port_dispatch_t *port_dispatch, uint8_t* out_port_id);

status_rep_t sys_gpio_set_mode(uint8_t port_id, uint8_t pin, uint32_t mode);
status_rep_t sys_gpio_set_level(uint8_t port_id, uint64_t pin_mask, bool level);
status_rep_t sys_gpio_read_level(uint8_t port_id, uint64_t pin_mask, uint64_t* out_levels);
status_rep_t sys_gpio_toggle(uint8_t port_id, uint64_t pin_mask);
status_rep_t sys_gpio_reset_pin(uint8_t port_id, uint8_t pin);
status_rep_t sys_gpio_register_callback(uint8_t port_id, uint8_t pin, uint32_t mode, void (*callback)(void* arg), void* arg);

status_rep_t sys_io_set_pwm_duty(uint8_t port_id, uint64_t pin_mask, uint32_t duty_cycle);
status_rep_t sys_io_set_pwm_freq(uint8_t port_id, uint64_t pin_mask, uint32_t freq_hz);

status_rep_t sys_io_adc_read(uint8_t port_id, uint64_t pin_mask, uint32_t* out_mv, uint8_t max_results_num);
status_rep_t sys_io_adc_register_callback(uint8_t port_id, uint8_t pin, void* adc_int_config);

status_rep_t sys_io_enable_global_protection(bool is_enabled);

/**
 * @brief Reset all IO providers to default state (callbacks, alert settings, PWM to 0)
 * Skips GPIO expander since it has dedicated reset pins
 * @return Status code
 */
status_rep_t sys_io_reset_all(void);

/**
 * Single-Pin Macro Wrappers
 * 
 * Pin Identifier (uint64_t) Encoding:
 * - Bits 8..15 : Port ID
 * - Bits 0..7  : Pin Index (0-63)
 */
#define SYS_IO_MAKE_PIN(port, pin) ((((uint16_t)(port)) << 8) | ((pin) & 0xFF))
#define SYS_IO_GET_PORT(pin) ((uint8_t)(((pin) >> 8) & 0xFF))
#define SYS_IO_GET_PIN(pin) ((uint8_t)((pin) & 0xFF))
#define SYS_IO_GET_MASK(pin) (1ULL << ((pin) & 0xFF))
#define SYS_IO_MAKE_INFO(port, pin, extra) \
    ((((uint64_t)(port) & 0xFF) << 8) | \
     (((uint64_t)(pin)  & 0xFF) )  | \
     ((((uint64_t)(extra) & 0xFF) << 16)))
#define SYS_IO_GET_INFO_EXTRA(info) ((uint8_t)((info) >> 16))

/**
 * @brief Single pin set mode, provide encoded pin (auto port fetch)
 */
#define SYS_GPIO_SET_MODE(pin, mode) \
    sys_gpio_set_mode(SYS_IO_GET_PORT(pin), SYS_IO_GET_PIN(pin), (mode))

/**
 * @brief Single pin set level, provide encoded pin (auto port fetch), and boolean level
 */
#define SYS_GPIO_SET_LEVEL(pin, level) \
    sys_gpio_set_level(SYS_IO_GET_PORT(pin), SYS_IO_GET_MASK(pin), (level))

/**
 * @brief Single pin read level, provide encoded pin (auto port fetch), and bool_ptr to receive level (true/false)
 */
#define SYS_GPIO_READ_LEVEL(pin, out_level_ptr) ({ \
    uint64_t _out_level_temp; \
    status_rep_t _read_result = sys_gpio_read_level(SYS_IO_GET_PORT(pin), SYS_IO_GET_MASK(pin), &_out_level_temp); \
    *(out_level_ptr) = (_out_level_temp & SYS_IO_GET_MASK(pin)) ? 1 : 0; \
    _read_result; \
})

/**
 * @brief Single pin toggle, provide encoded pin (auto port fetch)
 */
#define SYS_GPIO_TOGGLE(pin) \
    sys_gpio_toggle(SYS_IO_GET_PORT(pin), SYS_IO_GET_MASK(pin))

/**
 * @brief Single pin reset, provide encoded pin (auto port fetch)
 */
#define SYS_GPIO_RESET_PIN(pin) \
    sys_gpio_reset_pin(SYS_IO_GET_PORT(pin), SYS_IO_GET_PIN(pin))

/**
 * @brief Single pin register callback, provide encoded pin (auto port fetch)
 */
#define SYS_GPIO_REGISTER_CALLBACK(pin, mode, cb, arg) \
    sys_gpio_register_callback(SYS_IO_GET_PORT(pin), SYS_IO_GET_PIN(pin), (mode), (cb), (arg))


    #define SYS_IO_SET_PWM_DUTY(pin, duty) \
    sys_io_set_pwm_duty(SYS_IO_GET_PORT(pin), SYS_IO_GET_MASK(pin), (duty))

#define SYS_IO_SET_PWM_FREQ(pin, freq) \
    sys_io_set_pwm_freq(SYS_IO_GET_PORT(pin), SYS_IO_GET_MASK(pin), (freq))

/**
 * @brief Single pin ADC read, provide encoded pin (auto port fetch), and uint32_t_ptr to receive millivolt reading
 */
#define SYS_IO_ADC_READ(pin, out_mv) \
    sys_io_adc_read(SYS_IO_GET_PORT(pin), SYS_IO_GET_MASK(pin), (out_mv), 1)

/**
 * @brief Single pin ADC register callback, provide encoded pin (auto port fetch) and config pointer
 */
#define SYS_IO_ADC_REGISTER_CALLBACK(pin, config) \
    sys_io_adc_register_callback(SYS_IO_GET_PORT(pin), SYS_IO_GET_PIN(pin), (config))


