#pragma once

#include "sys_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The IO contract: not a module with state, but opcodes (contract
 * SYS_DEVICE_CONTRACT_IO: 0x01xx, the SYS_IO_CONTRACT table below) that mean the same on every device that has
 * them (a PWM expander, a GPIO expander, the ESP's own pins). A device
 * implements the ops it can in its table; the ops it has are its class. An op
 * it doesn't have answers ERR_DEV_UNKNOWN_OP. A device's pin is (device ID, pin);
 * using it is a call of that device's op, the same one a remote command makes.
 * The wrappers below build that call; they add nothing else.
 *
 * Payloads are packed little-endian structs. Duty is 0 .. IO_PWM_DUTY_MAX
 * (12-bit); a device with another resolution scales or rejects with
 * ERR_DEV_RANGE.
 */

#define IO_PWM_DUTY_MAX 4095u

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_IO): what a device with pins reports besides the
 * shared ones in sys_device.h (BAD_PIN, PIN_LOCKED, RANGE, SUSPENDED).
 * ------------------------------------------------------------------------- */

// clang-format off
#define ERR_SYS_IO_MAP(X)                                                                                                X(ERR_IO_PIN_IN_USE,             ERR_TAG(ERR_MOD_SYS_IO, 1), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; uint8_t mode; })  /* SET_MODE of a configured pin; mode = the current one */   X(ERR_IO_PIN_UNCONFIGURED,       ERR_TAG(ERR_MOD_SYS_IO, 2), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; })               /* the op needs SET_MODE first */   X(ERR_IO_WRONG_MODE,             ERR_TAG(ERR_MOD_SYS_IO, 3), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; uint8_t mode; })  /* the op doesn't fit the pin's current mode */   X(ERR_IO_MODE_UNSUPPORTED,       ERR_TAG(ERR_MOD_SYS_IO, 4), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; uint8_t mode; })  /* this pin / device can't do the mode */   X(ERR_IO_FEATURE_UNSUPPORTED,    ERR_TAG(ERR_MOD_SYS_IO, 5), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; })               /* e.g. ADC on a pin without ADC */   X(ERR_IO_PWM_CHANNELS_EXHAUSTED, ERR_TAG(ERR_MOD_SYS_IO, 6), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; uint8_t channels; })   X(ERR_IO_PWM_TIMERS_EXHAUSTED,   ERR_TAG(ERR_MOD_SYS_IO, 7), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; uint8_t timers; uint32_t frequency_Hz; }) /* a new frequency needs a timer and none is free */   X(ERR_IO_BAD_READING,            ERR_TAG(ERR_MOD_SYS_IO, 8), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; })               /* an ADC didn't return the channel asked for */
// clang-format on

ERR_TAGS_DECLARE(ERR_SYS_IO_MAP)

/*Aviable modes to set IO to*/
//#ref-enum @alias IO Mode
typedef enum sys_io_mode_e {
  SYS_IO_MODE_INPUT = 0,  //@alias Input @description Reads whether the pin is on or off - use for buttons, switches, and sensors that output a simple on/off signal
  SYS_IO_MODE_INPUT_PULLUP = 1,  //@alias Input with Pullup @description Same as Input, but reads "on" by default when nothing is connected - use when your button/switch connects the pin to ground when pressed
  SYS_IO_MODE_INPUT_PULLDOWN = 2,  //@alias Input with Pulldown @description Same as Input, but reads "off" by default when nothing is connected - use when your button/switch connects the pin to power when pressed
  SYS_IO_MODE_OUTPUT_PUSH_PULL = 3,  //@alias Output Push-Pull @description Standard output - the pin can actively switch between on and off. Use this for most outputs like LEDs and relays
  SYS_IO_MODE_OUTPUT_OPEN_DRAIN = 4,  //@alias Output Open-Drain @description A weaker output that can only actively pull the signal to "off" - something else (an external resistor, or the Open-Drain-Pullup option) is needed to make it read "on". Mainly used when several devices need to share the same wire
  SYS_IO_MODE_OUTPUT_OPEN_DRAIN_PULLUP = 5,  //@alias Output Open-Drain-Pullup @description Same as Open-Drain, but with a built-in helper enabled so the pin reads "on" by itself instead of needing an extra part
  SYS_IO_MODE_PWM = 6,  //@alias PWM @description Rapidly switches the pin on and off to fake an in-between level - use to dim an LED or control a motor's speed
  SYS_IO_MODE_ADC = 7,  //@alias ADC @description Measures the exact voltage on the pin instead of just on/off - use to read sensors that report a varying value, like a potentiometer or a temperature sensor
  SYS_IO_MODE_DAC = 8  //@alias DAC @description Outputs an exact, steady voltage level instead of just on/off - the opposite of ADC
} sys_io_mode_e;

/*Aviable interrupt modes*/
//#ref-enum @alias Interrupt Mode
typedef enum sys_io_intr_mode_e {
  SYS_IO_INTR_DISABLE = 0,  //@alias Disable @description Don't watch this pin for changes - nothing gets triggered
  SYS_IO_INTR_MODE_RISING_EDGE = 1,  //@alias Rising Edge @description Triggers the instant the pin switches from off to on
  SYS_IO_INTR_MODE_FALLING_EDGE = 2,  //@alias Falling Edge @description Triggers the instant the pin switches from on to off
  SYS_IO_INTR_MODE_BOTH_EDGES = 3,  //@alias Both Edges @description Triggers on any change - whether the pin switches from off to on, or on to off
  SYS_IO_INTR_ADC_WINDOW_OUTSIDE = 4,  //@alias Outside set window @description Triggers when the measured value leaves the safe/expected range you set - use to catch a value going too high or too low
  SYS_IO_INTR_ADC_WINDOW_INSIDE = 5,  //@alias Inside set window @description Triggers when the measured value comes back into the range you set - use to catch when a value returns to normal
} sys_io_intr_mode_e;

/* Events of the IO contract (sys_event.h "Events tables"), published by a
 * device for a pin armed with CONFIGURE_INTR; channel = pin. Flags 0: an edge
 * is code flow (an alert pin, a rule, the VM), not something to show by itself. */
// clang-format off
#define SYS_IO_EVENTS(X) \
  X(RISING, 0x00, 0)  /* @alias Rising edge @description A watched pin went from low to high @value The level (1) */ \
  X(FALLING, 0x01, 0)  /* @alias Falling edge @description A watched pin went from high to low @value The level (0) */ \
  X(ADC_OUTSIDE, 0x02, 0)  /* @alias Left window @description An analog value left its window @value The voltage @unit mV */ \
  X(ADC_INSIDE, 0x03, 0)  /* @alias Back inside window @description An analog value came back inside its window @value The voltage @unit mV */
// clang-format on

#define SYS_IO__EV(name, n, flags) \
  IO_EV_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_IO, n), IO_EV_##name##_FLAGS = (flags),

enum { SYS_IO_EVENTS(SYS_IO__EV) };

typedef struct __attribute__((packed)) {
  uint8_t pin;  //@alias Pin @description The pin of the device @id pin
} io_pin_args_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;  //@alias Pin @description The pin of the device @id pin
  uint8_t level;  //@alias Level @choices 0=Low,1=High
} io_level_args_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;  //@alias Pin @description The pin of the device @id pin
  uint32_t duty;  //@alias Duty @description PWM duty, 0 to 4095 (12 bits) @min 0 @max 4095
} io_duty_args_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;  //@alias Pin @description The pin of the device @id pin
  uint32_t frequency_Hz;  //@alias Frequency @unit Hz @min 1
} io_freq_args_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;  //@alias Pin @description The pin of the device @id pin
  uint8_t mode;  //@alias Mode @description What the pin does @enum-ref sys_io_mode_e
} io_mode_args_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;  //@alias Pin @description The pin of the device @id pin
  int32_t voltage_mV;  //@alias Voltage @unit mV
} io_voltage_args_t;

/**
 * @brief ADC window of the SYS_IO_INTR_ADC_WINDOW_* modes.
 */
typedef struct __attribute__((packed)) {
  uint16_t threshold_up_mV;  //@alias Upper threshold @unit mV
  uint16_t threshold_down_mV;  //@alias Lower threshold @unit mV
  uint16_t hysteresis_mV;  //@alias Hysteresis @unit mV
  uint16_t event_counter_threshold;  //@alias Readings to trigger @description Readings past the threshold before the event is raised
} sys_io_adc_window_t;

/**
 * @brief Interrupt config of one pin. It only arms the source; who gets the
 * pin's events is not part of the contract.
 */
typedef struct __attribute__((packed)) {
  uint8_t mode;  //@alias Watch for @description What makes the pin raise an event @enum-ref sys_io_intr_mode_e
  uint8_t debounce;  //@alias Debounce @description Filter switch bounce where the device can (off for a chip's alert line) @choices 0=Off,1=On
  sys_io_adc_window_t adc;  //@alias ADC window @description Used only by the window modes
} sys_io_intr_cfg_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;  //@alias Pin @description The pin of the device @id pin
  sys_io_intr_cfg_t cfg;  //@alias Interrupt @description What to watch the pin for
} io_intr_args_t;

/**
 * @brief The IO contract, once: X(NAME, op number, args struct). From it come the
 * op IDs IO_OP_<NAME>, a typedef io_args_<NAME>_t per op, IO_HANDLER(NAME) (a
 * device's handler: its args type is the one listed here) and IO_ENTRY(NAME) (its
 * row in the op table). Numbers are never reused.
 */
// clang-format off
#define SYS_IO_CONTRACT(X) \
  X(SET_LEVEL, 0x00, io_level_args_t)  /* @alias Set level @description Drives a pin high or low. */ \
  X(TOGGLE, 0x01, io_pin_args_t)  /* @alias Toggle @description Flips the level of a pin. */ \
  X(GET_LEVEL, 0x02, io_pin_args_t)  /* @alias Read level @description Reads the level of a pin. @returns u8 @returns-alias Level @choices 0=Low,1=High */ \
  X(SET_PWM_DUTY, 0x03, io_duty_args_t)  /* @alias Set PWM duty @description Sets the PWM duty of a pin. */ \
  X(SET_PWM_FREQ, 0x04, io_freq_args_t)  /* @alias Set PWM frequency @description Sets the PWM frequency of a pin; a device with one frequency for all its pins ignores the pin. */ \
  X(SET_MODE, 0x05, io_mode_args_t)  /* @alias Set mode @description Configures what a pin does. */ \
  X(RESET, 0x06, io_pin_args_t)  /* @alias Reset pin @description Puts a pin back in its power-on state. */ \
  X(CONFIGURE_INTR, 0x07, io_intr_args_t)  /* @alias Configure interrupt @description Arms a pin to raise events on an edge or when an analog value leaves a window. */ \
  X(GET_VOLTAGE, 0x08, io_pin_args_t)  /* @alias Read voltage @description Reads the voltage of an analog pin (a negative reading is reported). @returns i32 @returns-alias Voltage @unit mV */ \
  X(SET_VOLTAGE, 0x09, io_voltage_args_t)  /* @alias Set voltage @description Drives an analog output pin to a voltage. */
// clang-format on

#define SYS_IO__ID(name, n, args_t) IO_OP_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_IO, n),
#define SYS_IO__ARGS(name, n, args_t) typedef args_t io_args_##name##_t;

enum { SYS_IO_CONTRACT(SYS_IO__ID) };
SYS_IO_CONTRACT(SYS_IO__ARGS)

#define IO_HANDLER(name) SYS_DEVICE_HANDLER(io_h_##name, io_args_##name##_t)
#define IO_ENTRY(name) {IO_OP_##name, io_h_##name}

/**
 * @brief Pin of a device. In a create struct a pin that is not used is
 * SYS_IO_PIN_NONE: a zero-filled field is device 0 / pin 0, a real pin.
 */
typedef struct __attribute__((packed)) {
  uint16_t device;  //@alias Device @description The device the pin is on @id device @any 65535
  uint8_t pin;  //@alias Pin @description The pin of that device @id pin @device-field device
} sys_io_pin_t;

#define SYS_IO_PIN_NONE ((sys_io_pin_t){.device = SYS_DEVICE_ID_NONE, .pin = 0xFF})

static inline bool sys_io_pin_valid(sys_io_pin_t p) {
  return p.device != SYS_DEVICE_ID_NONE;
}

MUST_USE err_h sys_io_set_mode(sys_io_pin_t p, sys_io_mode_e mode);
MUST_USE err_h sys_io_reset(sys_io_pin_t p);
/**
 * @brief SET_MODE for a pin another driver uses: a device without SET_MODE (fixed-function pins: PCA9685 PWM channels,
 * DAC outputs) is fine, any other failure is returned.
 */
MUST_USE err_h sys_io_prepare(sys_io_pin_t p, sys_io_mode_e mode);
/**
 * @brief An open-drain alert / interrupt line as an input: with the pull-up where the device has one (ESP GPIO), else
 * plain (an expander such as the TCA6424A: the board has an external pull-up).
 */
MUST_USE err_h sys_io_prepare_input(sys_io_pin_t p);
/** @brief RESET for a pin another driver used: same tolerance as sys_io_prepare. */
MUST_USE err_h sys_io_release(sys_io_pin_t p);
MUST_USE err_h sys_io_configure_intr(sys_io_pin_t p, const sys_io_intr_cfg_t* cfg);

MUST_USE err_h sys_io_set_level(sys_io_pin_t p, bool level);
MUST_USE err_h sys_io_toggle(sys_io_pin_t p);
MUST_USE err_h sys_io_get_level(sys_io_pin_t p, bool* level);

MUST_USE err_h sys_io_get_voltage(sys_io_pin_t p, int32_t* out_mV);
MUST_USE err_h sys_io_set_voltage(sys_io_pin_t p, int32_t voltage_mV);

MUST_USE err_h sys_io_set_pwm_duty(sys_io_pin_t p, uint32_t duty);
MUST_USE err_h sys_io_set_pwm_frequency(sys_io_pin_t p, uint32_t frequency_Hz);

#ifdef __cplusplus
}
#endif
