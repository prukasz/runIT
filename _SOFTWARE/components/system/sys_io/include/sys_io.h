#pragma once
#include "status.h"
#include "sys_callbacks.h"

#define SYS_GPIO_NONE 0xFF
#define IF_PIN(pin_num) if (((pin_num)) != SYS_GPIO_NONE)
#define SYS_IO_HIGH(device_id, pin_num) sys_io_set_level((device_id), (pin_num), true)
#define SYS_IO_LOW(device_id, pin_num) sys_io_set_level((device_id), (pin_num), false)

#define SYS_IO_STA_W(code, owner, info) STA_W((code), (owner), (info), STATUS_PAYLOAD_SYS_IO)
#define SYS_IO_STA_C(code, owner, info) STA_C((code), (owner), (info), STATUS_PAYLOAD_SYS_IO)
#define SYS_IO_STA_I(code, owner, info) STA_I((code), (owner), (info), STATUS_PAYLOAD_SYS_IO)

#define SYS_IO_CHECK_NOT_NULL_R(ptr) CHECK_NOT_NULL_X((ptr), 1, 0, 0, STATUS_PAYLOAD_SYS_IO)
#define SYS_IO_CHECK_NOT_NULL_RP(ptr) CHECK_NOT_NULL_X((ptr), 1, 1, 0, STATUS_PAYLOAD_SYS_IO)
#define SYS_IO_CHECK_HANDLE_R(handle) CHECK_HANDLE_X((handle), 1, 0, 0, STATUS_PAYLOAD_SYS_IO)
#define SYS_IO_CHECK_HANDLE_RP(handle) CHECK_HANDLE_X((handle), 1, 1, 0, STATUS_PAYLOAD_SYS_IO)

#define SYS_IO_MAKE_INFO(dev_id, pin_num, extra) (((uint64_t)(dev_id) << 40) | ((uint64_t)(pin_num) << 32) | ((uint64_t)(extra) & 0xFFFFFFFF))

#define SYS_IO_UNPACK_DEV_ID(info) (((uint64_t)(info) >> 40) & 0xFF)
#define SYS_IO_UNPACK_PIN(info) (((uint64_t)(info) >> 32) & 0xFF)
#define SYS_IO_UNPACK_EXTRA(info) ((uint64_t)(info) & 0xFFFFFFFF)

#define SYS_IO_TOGGLE(device_id, pin_num) sys_io_toggle((device_id), (pin_num))

#define SYS_IO_CB(_ctx, _pin, _event, _value, _route_mask)    \
  do {                                                        \
    cb_event_t __cb_evt;                                      \
    memset(&__cb_evt, 0, sizeof(__cb_evt));                   \
    __cb_evt.head.callback_type = CALLBACK_IO;                \
    __cb_evt.head.route_to.route_mask = (_route_mask);        \
    __cb_evt.event.io.device_id = (_ctx)->base.device_id;     \
    __cb_evt.event.io.pin_id = (_pin);                        \
    __cb_evt.event.io.trigger_event = (_event);               \
    __cb_evt.event.io.trigger_value = (_value);               \
    sys_callback_trigger(&__cb_evt);                          \
  } while (0)

#define VERIFY_PIN_R(pin, pinmask)                                                                                \
  do {                                                                                                            \
    if (((pin) >= 64) || !((1ULL << (pin)) & (pinmask))) {                                                        \
      return SYS_IO_STA_W(ERR_SYS_IO_PIN_DOES_NOT_EXIST, OWNER, SYS_IO_MAKE_INFO(ctx->base.device_id, (pin), 0)); \
    }                                                                                                             \
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
  own_funct_t own_func;
  union {
    sys_io_adc_int_config_t adc;
  };
} sys_io_intr_config_t;

typedef struct sys_io_vtable_t {
  status_rep_t (*io_reset)(void* handle, sys_io_pin_num_t pin);

  status_rep_t (*io_set_mode)(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode);

  status_rep_t (*io_configure_intr)(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config);

  status_rep_t (*io_set_level)(void* handle, sys_io_pin_num_t pin, bool level);
  status_rep_t (*io_get_level)(void* handle, sys_io_pin_num_t pin, bool* level);
  status_rep_t (*io_toggle)(void* handle, sys_io_pin_num_t pin);

  status_rep_t (*io_get_voltage)(void* handle, sys_io_pin_num_t pin, uint32_t* out_mV);
  status_rep_t (*io_set_voltage)(void* handle, sys_io_pin_num_t pin, uint32_t voltage_mV);

  status_rep_t (*io_set_pwm_frequency)(void* handle, sys_io_pin_num_t pin, uint32_t frequency_HZ);
  status_rep_t (*io_set_pwm_duty)(void* handle, sys_io_pin_num_t pin, uint32_t duty);

  uint64_t protected_pins;
} sys_io_vtable_t;

typedef struct sys_io_device_t {
  sys_io_vtable_t* dispatch_table;
  void* handle;
} sys_io_device_t;

// API Systemowe używa teraz wyłącznie uint8_t device_id i pinu
status_rep_t sys_io_reset(uint8_t device_id, sys_io_pin_num_t pin);
status_rep_t sys_io_set_mode(uint8_t device_id, sys_io_pin_num_t pin, sys_io_mode_e mode);
status_rep_t sys_io_configure_intr(uint8_t device_id, sys_io_pin_num_t pin, const sys_io_intr_config_t* config);
status_rep_t sys_io_set_level(uint8_t device_id, sys_io_pin_num_t pin, bool level);
status_rep_t sys_io_get_level(uint8_t device_id, sys_io_pin_num_t pin, bool* level);
status_rep_t sys_io_toggle(uint8_t device_id, sys_io_pin_num_t pin);

status_rep_t sys_io_get_voltage(uint8_t device_id, sys_io_pin_num_t pin, uint32_t* out_mV);
status_rep_t sys_io_set_voltage(uint8_t device_id, sys_io_pin_num_t pin, uint32_t voltage_mV);

status_rep_t sys_io_set_pwm_frequency(uint8_t device_id, sys_io_pin_num_t pin, uint32_t frequency_HZ);
status_rep_t sys_io_set_pwm_duty(uint8_t device_id, sys_io_pin_num_t pin, uint32_t duty);

status_rep_t sys_io_register_driver(uint8_t device_id, void* handle, sys_io_vtable_t* dispatch_table);
status_rep_t sys_io_unregister_driver(uint8_t device_id);

#define SYS_IO_UNLOCK_PIN(dev_id, pin)                                                            \
  do {                                                                                            \
    sys_device_t* __d = sys_device_get_by_id((dev_id));                                           \
    sys_io_vtable_t* __v = __d ? (sys_io_vtable_t*)__d->contracts[SYS_DEVICE_CONTRACT_IO] : NULL; \
    if (__v) __v->protected_pins &= ~(1ULL << (pin));                                             \
  } while (0)

#define SYS_IO_LOCK_PIN(dev_id, pin)                                                              \
  do {                                                                                            \
    sys_device_t* __d = sys_device_get_by_id((dev_id));                                           \
    sys_io_vtable_t* __v = __d ? (sys_io_vtable_t*)__d->contracts[SYS_DEVICE_CONTRACT_IO] : NULL; \
    if (__v) __v->protected_pins |= (1ULL << (pin));                                              \
  } while (0)

#define WITH_PIN_UNLOCKED(dev_id, pin)                                                                                                         \
  for (sys_io_vtable_t* __v = (sys_io_vtable_t*)SYS_DEV_GET_CONTRACT(sys_device_get_by_id((dev_id)), SYS_DEVICE_CONTRACT_IO); __v; __v = NULL) \
    for (uint64_t __mask = (1ULL << (pin)), __prev = (__v->protected_pins & __mask ? (__v->protected_pins &= ~__mask, __mask) : 0); __mask; __mask = (__prev ? (__v->protected_pins |= __mask, 0) : 0))

extern const char* const sys_io_mode_e_to_string[];
extern const char* const sys_io_intr_mode_e_to_string[];
