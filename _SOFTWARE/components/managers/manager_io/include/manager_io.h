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

typedef enum{
    SYS_GPIO_MODE_RISING_EDGE = 0,
    SYS_GPIO_MODE_FALLING_EDGE = 1,
    SYS_GPIO_MODE_BOTH_EDGES = 2,
    SYS_GPIO_MODE_LEVEL_HIGH = 3,
    SYS_GPIO_MODE_LEVEL_LOW = 4
}sys_gpio_int_mode_e;

typedef enum{
    SYS_GPIO_ADC_WINDOW_OUTSIDE = 0,
    SYS_GPIO_ADC_WINDOW_INSIDE = 1,
}sys_gpio_adc_int_mode_e;

typedef struct{
    uint32_t adc_threshold_up_mv;
    uint32_t adc_threshold_down_mv;
    uint32_t adc_threshold_hysteresis_mv;
    uint32_t adc_event_counter_threshold;
    uint32_t adc_window_mode; //0: outside window, 1: inside window
    //add here window type 
    void (*callback)(void* arg);
    void* arg;
}sys_io_adc_int_config_t;

typedef status_rep_t (*gpio_func_mode)(uint64_t pin_mask, uint32_t mode);
typedef status_rep_t (*gpio_func_set)(uint64_t pin_mask, bool state);
typedef status_rep_t (*gpio_func_read)(uint64_t pin_mask, bool* out_state);
typedef status_rep_t (*gpio_func_toggle)(uint64_t pin_mask);
typedef status_rep_t (*gpio_func_callback_add)(uint64_t pin_mask, uint32_t mode, void (*callback)(void* arg), void* arg);
typedef status_rep_t (*io_func_pwm_set_duty)(uint64_t pin_mask, uint32_t duty_cycle);
typedef status_rep_t (*io_func_pwm_set_freq)(uint64_t pin_mask, uint32_t freq_hz);
typedef status_rep_t (*io_func_adc_read)(uint64_t pin_mask, uint32_t* out_mv);
typedef status_rep_t (*io_func_adc_register_callback)(uint64_t pin_mask, void* adc_int_config);
typedef void (*io_driver_dereffered_update)(bool yes_or_no);

typedef struct {
    gpio_func_mode mode_func;
    gpio_func_set set_func;
    gpio_func_read read_func;
    gpio_func_toggle toggle_func;
    gpio_func_callback_add callback_add_func;
    io_func_pwm_set_duty pwm_set_duty_func;
    io_func_pwm_set_freq pwm_set_freq_func;
    io_func_adc_read adc_read_func;
    io_func_adc_register_callback adc_callback_add_func;
    io_driver_dereffered_update dereffered_update;
    uint64_t protected_pins;
} io_port_dispatch_t;


void manager_io_enter_mode_dereffered(void);
void manager_io_exit_mode_dereffered(void);

status_rep_t manager_io_register_new_port(io_port_dispatch_t *port_dispatch, uint8_t* out_port_id);

status_rep_t sys_gpio_set_mode(uint8_t port_id, uint64_t pin_mask, uint32_t mode);
status_rep_t sys_gpio_set_level(uint8_t port_id, uint64_t pin_mask, bool level);
status_rep_t sys_gpio_read_level(uint8_t port_id, uint64_t pin_mask, bool* level);
status_rep_t sys_gpio_toggle(uint8_t port_id, uint64_t pin_mask);
status_rep_t sys_gpio_register_callback(uint8_t port_id, uint64_t pin_mask, uint32_t mode, void (*callback)(void* arg), void* arg);

status_rep_t sys_io_set_pwm_duty(uint8_t port_id, uint64_t pin_mask, uint32_t duty_cycle);
status_rep_t sys_io_set_pwm_freq(uint8_t port_id, uint64_t pin_mask, uint32_t freq_hz);

status_rep_t sys_io_adc_read(uint8_t port_id, uint64_t pin_mask, uint32_t* out_mv);
status_rep_t sys_io_adc_register_callback(uint8_t port_id, uint64_t pin_mask, void* adc_int_config);

status_rep_t sys_io_set_global_protection(bool is_enabled);

/**
 * Single-Pin Macro Wrappers
 * 
 * Pin Identifier (uint32_t) Encoding:
 * - Bits 8..15 : Port ID
 * - Bits 0..7  : Pin Index (0-63)
 */
#define SYS_IO_MAKE_PIN(port, pin) ((((uint32_t)(port)) << 8) | ((pin) & 0xFF))
#define SYS_IO_GET_PORT(pin32) ((uint8_t)(((pin32) >> 8) & 0xFF))
#define SYS_IO_GET_MASK(pin32) (1ULL << ((pin32) & 0x3F))

#define SYS_GPIO_SET_MODE(pin32, mode) \
    sys_gpio_set_mode(SYS_IO_GET_PORT(pin32), SYS_IO_GET_MASK(pin32), (mode))

#define SYS_GPIO_SET_LEVEL(pin32, level) \
    sys_gpio_set_level(SYS_IO_GET_PORT(pin32), SYS_IO_GET_MASK(pin32), (level))

#define SYS_GPIO_READ_LEVEL(pin32, out_level) \
    sys_gpio_read_level(SYS_IO_GET_PORT(pin32), SYS_IO_GET_MASK(pin32), (out_level))

#define SYS_GPIO_TOGGLE(pin32) \
    sys_gpio_toggle(SYS_IO_GET_PORT(pin32), SYS_IO_GET_MASK(pin32))

#define SYS_GPIO_REGISTER_CALLBACK(pin32, mode, cb, arg) \
    sys_gpio_register_callback(SYS_IO_GET_PORT(pin32), SYS_IO_GET_MASK(pin32), (mode), (cb), (arg))

#define SYS_IO_SET_PWM_DUTY(pin32, duty) \
    sys_io_set_pwm_duty(SYS_IO_GET_PORT(pin32), SYS_IO_GET_MASK(pin32), (duty))

#define SYS_IO_SET_PWM_FREQ(pin32, freq) \
    sys_io_set_pwm_freq(SYS_IO_GET_PORT(pin32), SYS_IO_GET_MASK(pin32), (freq))

#define SYS_IO_ADC_READ(pin32, out_mv) \
    sys_io_adc_read(SYS_IO_GET_PORT(pin32), SYS_IO_GET_MASK(pin32), (out_mv))

#define SYS_IO_ADC_REGISTER_CALLBACK(pin32, config) \
    sys_io_adc_register_callback(SYS_IO_GET_PORT(pin32), SYS_IO_GET_MASK(pin32), (config))


