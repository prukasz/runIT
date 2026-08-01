#pragma once
#include "sys_callbacks.h"
#include "sys_error.h"

#define SYS_GPIO_NONE 0xFF
#define IF_PIN(pin_num) if (((pin_num)) != SYS_GPIO_NONE)

#define SYS_IO_CB(_ctx, _pin, _event, _value, _route_mask, _action_mask) \
  do {                                                                   \
    cb_event_t __cb_evt;                                                \
    memset(&__cb_evt, 0, sizeof(__cb_evt));                             \
    __cb_evt.head.callback_type = CALLBACK_IO;                          \
    __cb_evt.head.route_mask = (_route_mask);                           \
    __cb_evt.head.action_id = (_action_mask);                           \
    __cb_evt.event.io.device_id = (_ctx)->base.device_id;               \
    __cb_evt.event.io.pin_id = (_pin);                                  \
    __cb_evt.event.io.trigger_event = (_event);                         \
    __cb_evt.event.io.trigger_value = (_value);                         \
    sys_callback_trigger(&__cb_evt);                                    \
  } while (0)

#define VERIFY_PIN(dev_id, pin, pinmask)                   \
  do {                                                     \
    if (((pin) >= 64) || !((1ULL << (pin)) & (pinmask))) { \
      SE_RET_ERR(ERR_IO_PIN_UNAVAILABLE, (dev_id), (pin)); \
    }                                                      \
  } while (0)

/*Aviable modes to set IO to*/
typedef enum sys_io_mode_e {
  SYS_IO_MODE_INPUT = 0,
  SYS_IO_MODE_INPUT_PULLUP = 1,
  SYS_IO_MODE_INPUT_PULLDOWN = 2,
  SYS_IO_MODE_OUTPUT_PUSH_PULL = 3,
  SYS_IO_MODE_OUTPUT_OPEN_DRAIN = 4,
  SYS_IO_MODE_OUTPUT_OPEN_DRAIN_PULLUP = 5,
  SYS_IO_MODE_PWM = 6,
  SYS_IO_MODE_ADC = 7,
  SYS_IO_MODE_DAC = 8
} sys_io_mode_e;

/*Aviable interrupt modes*/
typedef enum sys_io_intr_mode_e {
  SYS_IO_INTR_DISABLE = 0,
  SYS_IO_INTR_MODE_RISING_EDGE = 1,
  SYS_IO_INTR_MODE_FALLING_EDGE = 2,
  SYS_IO_INTR_MODE_BOTH_EDGES = 3,
  SYS_IO_INTR_ADC_WINDOW_OUTSIDE = 4,
  SYS_IO_INTR_ADC_WINDOW_INSIDE = 5,
} sys_io_intr_mode_e;

typedef uint8_t sys_io_pin_num_t;

/**
 * @brief Reference to a pin on some IO device: the (device, pin, mode) triple.
 *
 * Collapses the three positional arguments device configs used to pass
 * separately, so callers name what they mean instead of counting.
 *
 * @warning An unused pin MUST be spelled SYS_IO_PIN_NONE. Omitting a pin ref
 *          from a designated initializer zero-fills it to {device 0, pin 0,
 *          SYS_IO_MODE_INPUT} - device 0 is a real device and pin 0 is a real
 *          pin, so an omission silently means "drive pin 0", not "unused".
 */
typedef struct sys_io_pin_ref_t {
  uint8_t device_id;
  sys_io_pin_num_t pin; /* SYS_GPIO_NONE when unused */
  sys_io_mode_e mode;
} sys_io_pin_ref_t;

/*Compound-literal forms, for automatic storage (inside function bodies)*/
#define SYS_IO_PIN(dev_id, pin_num, pin_mode) ((sys_io_pin_ref_t){.device_id = (dev_id), .pin = (pin_num), .mode = (pin_mode)})
#define SYS_IO_PIN_NONE ((sys_io_pin_ref_t){.pin = SYS_GPIO_NONE})

/*Brace-only forms, for file-scope/static initializers where a compound
  literal is not a constant expression*/
#define SYS_IO_PIN_INIT(dev_id, pin_num, pin_mode) {.device_id = (dev_id), .pin = (pin_num), .mode = (pin_mode)}
#define SYS_IO_PIN_NONE_INIT {.pin = SYS_GPIO_NONE}

/*interrupt config for adc*/
typedef struct {
  uint16_t adc_threshold_up_mV;
  uint16_t adc_threshold_down_mV;
  uint16_t adc_threshold_hysteresis_mV;
  uint16_t adc_event_counter_threshold;
} sys_io_adc_int_config_t;

/*overall config for io interrupt*/
typedef struct sys_io_intr_config_t {
  sys_io_intr_mode_e mode;
  uint16_t route_mask;
  uint64_t action_mask; /* Bitmask of sys_actions ids to invoke: 0 means none */
  own_funct_t own_func;
  union {
    sys_io_adc_int_config_t adc;
  };
} sys_io_intr_config_t;

typedef struct sys_io_vtable_t {
  err_h (*io_reset)(void* handle, sys_io_pin_num_t pin);

  err_h (*io_set_mode)(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode);

  err_h (*io_configure_intr)(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config);

  err_h (*io_set_level)(void* handle, sys_io_pin_num_t pin, bool level);
  err_h (*io_get_level)(void* handle, sys_io_pin_num_t pin, bool* level);
  err_h (*io_toggle)(void* handle, sys_io_pin_num_t pin);

  err_h (*io_get_voltage)(void* handle, sys_io_pin_num_t pin, uint32_t* out_mV);
  err_h (*io_set_voltage)(void* handle, sys_io_pin_num_t pin, uint32_t voltage_mV);

  err_h (*io_set_pwm_frequency)(void* handle, sys_io_pin_num_t pin, uint32_t frequency_HZ);
  err_h (*io_set_pwm_duty)(void* handle, sys_io_pin_num_t pin, uint32_t duty);

  uint64_t protected_pins;
} sys_io_vtable_t;

typedef struct sys_io_device_t {
  sys_io_vtable_t* dispatch_table;
  void* handle;
} sys_io_device_t;

// API Systemowe używa teraz wyłącznie uint8_t device_id i pinu
err_h sys_io_reset(uint8_t device_id, sys_io_pin_num_t pin);
err_h sys_io_set_mode(uint8_t device_id, sys_io_pin_num_t pin, sys_io_mode_e mode);
err_h sys_io_configure_intr(uint8_t device_id, sys_io_pin_num_t pin, const sys_io_intr_config_t* config);

err_h sys_io_set_level(uint8_t device_id, sys_io_pin_num_t pin, bool level);
err_h sys_io_get_level(uint8_t device_id, sys_io_pin_num_t pin, bool* level);
err_h sys_io_toggle(uint8_t device_id, sys_io_pin_num_t pin);

err_h sys_io_get_voltage(uint8_t device_id, sys_io_pin_num_t pin, uint32_t* out_mV);
err_h sys_io_set_voltage(uint8_t device_id, sys_io_pin_num_t pin, uint32_t voltage_mV);

err_h sys_io_set_pwm_frequency(uint8_t device_id, sys_io_pin_num_t pin, uint32_t frequency_HZ);
err_h sys_io_set_pwm_duty(uint8_t device_id, sys_io_pin_num_t pin, uint32_t duty);
#define SYS_IO_HIGH(device_id, pin_num) sys_io_set_level((device_id), (pin_num), true)
#define SYS_IO_LOW(device_id, pin_num) sys_io_set_level((device_id), (pin_num), false)

#define SYS_IO_UNLOCK_PIN(dev_id, pin)                                                                 \
  do {                                                                                                 \
    sys_device_t* __d = sys_device_get_by_id((dev_id));                                                \
    sys_io_vtable_t* __v = __d ? (sys_io_vtable_t*)__d->cls->contracts[SYS_DEVICE_CONTRACT_IO] : NULL; \
    if (__v) __v->protected_pins &= ~(1ULL << (pin));                                                  \
  } while (0)

#define SYS_IO_LOCK_PIN(dev_id, pin)                                                                   \
  do {                                                                                                 \
    sys_device_t* __d = sys_device_get_by_id((dev_id));                                                \
    sys_io_vtable_t* __v = __d ? (sys_io_vtable_t*)__d->cls->contracts[SYS_DEVICE_CONTRACT_IO] : NULL; \
    if (__v) __v->protected_pins |= (1ULL << (pin));                                                   \
  } while (0)

#define WITH_PIN_UNLOCKED(dev_id, pin)                                                                                                         \
  for (sys_io_vtable_t* __v = (sys_io_vtable_t*)SYS_DEV_GET_CONTRACT(sys_device_get_by_id((dev_id)), SYS_DEVICE_CONTRACT_IO); __v; __v = NULL) \
    for (uint64_t __mask = (1ULL << (pin)), __prev = (__v->protected_pins & __mask ? (__v->protected_pins &= ~__mask, __mask) : 0); __mask; __mask = (__prev ? (__v->protected_pins |= __mask, 0) : 0))

/*sys_io_pin_ref_t operations. Pass an lvalue only - (ref) is evaluated more than once.*/
#define IF_PIN_REF(ref) IF_PIN((ref).pin)
#define SYS_IO_REF_SET_MODE(ref) sys_io_set_mode((ref).device_id, (ref).pin, (ref).mode)
#define SYS_IO_REF_HIGH(ref) sys_io_set_level((ref).device_id, (ref).pin, true)
#define SYS_IO_REF_LOW(ref) sys_io_set_level((ref).device_id, (ref).pin, false)
#define SYS_IO_REF_RESET(ref) sys_io_reset((ref).device_id, (ref).pin)
#define SYS_IO_REF_LOCK(ref) SYS_IO_LOCK_PIN((ref).device_id, (ref).pin)
#define SYS_IO_REF_UNLOCK(ref) SYS_IO_UNLOCK_PIN((ref).device_id, (ref).pin)
#define WITH_REF_UNLOCKED(ref) WITH_PIN_UNLOCKED((ref).device_id, (ref).pin)

extern const char* const sys_io_mode_e_to_string[];
extern const char* const sys_io_intr_mode_e_to_string[];
