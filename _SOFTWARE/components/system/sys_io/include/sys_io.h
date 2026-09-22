#pragma once
#include <string.h>
#include "sys_event.h"
#include "sys_error.h"

#define SYS_GPIO_NONE 0xFF


#define VERIFY_PIN(dev_id, pin, pinmask)                   \
  do {                                                     \
    if (((pin) >= 64) || !((1ULL << (pin)) & (pinmask))) { \
      SE_FAIL(ERR_IO_PIN_UNAVAILABLE, (dev_id), (pin)); \
    }                                                      \
  } while (0)

/*Aviable modes to set IO to*/
//#ref-enum @alias IO Mode
typedef enum sys_io_mode_e {
  SYS_IO_MODE_INPUT = 0, // @alias Input @description Reads whether the pin is on or off - use for buttons, switches, and sensors that output a simple on/off signal
  SYS_IO_MODE_INPUT_PULLUP = 1, // @alias Input with Pullup @description Same as Input, but reads "on" by default when nothing is connected - use when your button/switch connects the pin to ground when pressed
  SYS_IO_MODE_INPUT_PULLDOWN = 2,  // @alias Input with Pulldown @description Same as Input, but reads "off" by default when nothing is connected - use when your button/switch connects the pin to power when pressed
  SYS_IO_MODE_OUTPUT_PUSH_PULL = 3, // @alias Output Push-Pull @description Standard output - the pin can actively switch between on and off. Use this for most outputs like LEDs and relays
  SYS_IO_MODE_OUTPUT_OPEN_DRAIN = 4, //@alias Output Open-Drain @description A weaker output that can only actively pull the signal to "off" - something else (an external resistor, or the Open-Drain-Pullup option) is needed to make it read "on". Mainly used when several devices need to share the same wire
  SYS_IO_MODE_OUTPUT_OPEN_DRAIN_PULLUP = 5, //@alias Output Open-Drain-Pullup @description Same as Open-Drain, but with a built-in helper enabled so the pin reads "on" by itself instead of needing an extra part
  SYS_IO_MODE_PWM = 6, //@alias PWM @description Rapidly switches the pin on and off to fake an in-between level - use to dim an LED or control a motor's speed
  SYS_IO_MODE_ADC = 7, //@alias ADC @description Measures the exact voltage on the pin instead of just on/off - use to read sensors that report a varying value, like a potentiometer or a temperature sensor
  SYS_IO_MODE_DAC = 8  //@alias DAC @description Outputs an exact, steady voltage level instead of just on/off - the opposite of ADC
} sys_io_mode_e;

/*Aviable interrupt modes*/
//#ref-enum @alias Interrupt Mode
typedef enum sys_io_intr_mode_e {
  SYS_IO_INTR_DISABLE = 0, //@alias Disable @description Don't watch this pin for changes - nothing gets triggered
  SYS_IO_INTR_MODE_RISING_EDGE = 1, //@alias Rising Edge @description Triggers the instant the pin switches from off to on
  SYS_IO_INTR_MODE_FALLING_EDGE = 2, //@alias Falling Edge @description Triggers the instant the pin switches from on to off
  SYS_IO_INTR_MODE_BOTH_EDGES = 3, //@alias Both Edges @description Triggers on any change - whether the pin switches from off to on, or on to off
  SYS_IO_INTR_ADC_WINDOW_OUTSIDE = 4, //@alias Outside set window @description Triggers when the measured value leaves the safe/expected range you set - use to catch a value going too high or too low
  SYS_IO_INTR_ADC_WINDOW_INSIDE = 5, //@alias Inside set window @description Triggers when the measured value comes back into the range you set - use to catch when a value returns to normal
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
#define SYS_IO_REF(dev_id, pin_num) ((sys_io_pin_ref_t){.device_id = (dev_id), .pin = (pin_num)})

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

/* Interrupt config of one pin. It only arms the source: events go to the
   sys_event listeners of (SYS_EVENT_DOMAIN_IO, device, pin). */
typedef struct sys_io_intr_config_t {
  sys_io_intr_mode_e mode;
  bool debounce; /* filter switch bounce where the device supports it; off for chip alert lines */
  union {
    sys_io_adc_int_config_t adc;
  };
} sys_io_intr_config_t;

typedef struct sys_io_contract_t {
  err_h (*reset)(void* handle, sys_io_pin_num_t pin);

  err_h (*set_mode)(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode);

  err_h (*configure_intr)(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config);

  err_h (*set_level)(void* handle, sys_io_pin_num_t pin, bool level);
  err_h (*get_level)(void* handle, sys_io_pin_num_t pin, bool* level);
  err_h (*toggle)(void* handle, sys_io_pin_num_t pin);

  err_h (*get_voltage)(void* handle, sys_io_pin_num_t pin, int32_t* out_mV);
  err_h (*set_voltage)(void* handle, sys_io_pin_num_t pin, uint32_t voltage_mV);

  err_h (*set_pwm_frequency)(void* handle, sys_io_pin_num_t pin, uint32_t frequency_Hz);
  err_h (*set_pwm_duty)(void* handle, sys_io_pin_num_t pin, uint32_t duty);
} sys_io_contract_t;

/**
 * @brief Identifies which sys_io_contract_t slot a NULL-vtable-function
 * dispatch failure was for - carried as ERR_DEV_FEATURE_UNAVAILABLE's
 * feature_id payload field (see SYS_IO_DISPATCH), so the failure says
 * *which* IO operation is unsupported instead of a bare "not supported".
 */
/* Member names of sys_io_contract_t in order, NULL-terminated (feature id = index). */
extern const char* const sys_io_feature_names[];

SE_MUST_USE err_h sys_io_reset(sys_io_pin_ref_t ref);
SE_MUST_USE err_h sys_io_set_mode(sys_io_pin_ref_t ref);
SE_MUST_USE err_h sys_io_configure_intr(sys_io_pin_ref_t ref, const sys_io_intr_config_t* config);

SE_MUST_USE err_h sys_io_set_level(sys_io_pin_ref_t ref, bool level);
SE_MUST_USE err_h sys_io_get_level(sys_io_pin_ref_t ref, bool* level);
SE_MUST_USE err_h sys_io_toggle(sys_io_pin_ref_t ref);

SE_MUST_USE err_h sys_io_get_voltage(sys_io_pin_ref_t ref, int32_t* out_mV);
SE_MUST_USE err_h sys_io_set_voltage(sys_io_pin_ref_t ref, uint32_t voltage_mV);

SE_MUST_USE err_h sys_io_set_pwm_frequency(sys_io_pin_ref_t ref, uint32_t frequency_Hz);
SE_MUST_USE err_h sys_io_set_pwm_duty(sys_io_pin_ref_t ref, uint32_t duty);

SE_MUST_USE err_h sys_io_lock_pin(sys_io_pin_ref_t ref);
SE_MUST_USE err_h sys_io_unlock_pin(sys_io_pin_ref_t ref);

/**
 * @brief Set the level of a pin the caller has locked (OE, RST, EN, ...).
 *
 * Unlocks the pin only if it was locked, sets the level, and restores the
 * previous lock state on every path, including failure.
 */
SE_MUST_USE err_h sys_io_set_locked_level(sys_io_pin_ref_t ref, bool level);

/**
 * @brief Publish a pin event from an IO device adapter (task or ISR).
 * @param event The edge or window crossed: RISING / FALLING edge for a
 *        digital pin, the armed mode for an analog one.
 * @param value Level (0 / 1) or mV.
 * @param hops SYS_EVENT_CAUSED_BY(cause) when raised from a listener, else 0.
 */
static inline SE_MUST_USE err_h sys_io_publish(uint8_t device_id, sys_io_pin_num_t pin, sys_io_intr_mode_e event, int32_t value, uint8_t hops) {
  sys_event_t ev = {.domain = SYS_EVENT_DOMAIN_IO, .device_id = device_id, .channel = pin, .event = (uint8_t)event, .value = value, .hops = hops};
  return sys_event_publish(&ev);
}

/**
 * @brief Chain a device to an alert pin: call handler inline on every event
 * of that pin (the adapter then reads its chip and publishes its own events).
 * System-owned; remove it with sys_event_unsubscribe(*out_id, false).
 */
SE_MUST_USE err_h sys_io_subscribe_pin(sys_io_pin_ref_t ref, sys_event_handler_f handler, void* ctx, uint8_t* out_id);

/* Type-safe sys_io_pin_ref_t operations */
static inline bool sys_io_pin_is_valid(sys_io_pin_ref_t ref) {
  return ref.pin != SYS_GPIO_NONE;
}

extern const char* const sys_io_mode_e_to_string[];
extern const char* const sys_io_intr_mode_e_to_string[];
