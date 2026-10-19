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

#define ERR_SYS_IO_MAP(X)                                                                                                                                                                             \
  X(ERR_IO_PIN_IN_USE,             ERR_TAG(ERR_MOD_SYS_IO, 1), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; uint8_t mode; })  /* SET_MODE of a configured pin; mode = the current one */                   \
  X(ERR_IO_PIN_UNCONFIGURED,       ERR_TAG(ERR_MOD_SYS_IO, 2), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; })               /* the op needs SET_MODE first */                                             \
  X(ERR_IO_WRONG_MODE,             ERR_TAG(ERR_MOD_SYS_IO, 3), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; uint8_t mode; })  /* the op doesn't fit the pin's current mode */                              \
  X(ERR_IO_MODE_UNSUPPORTED,       ERR_TAG(ERR_MOD_SYS_IO, 4), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; uint8_t mode; })  /* this pin / device can't do the mode */                                    \
  X(ERR_IO_FEATURE_UNSUPPORTED,    ERR_TAG(ERR_MOD_SYS_IO, 5), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; })               /* e.g. ADC on a pin without ADC */                                           \
  X(ERR_IO_PWM_CHANNELS_EXHAUSTED, ERR_TAG(ERR_MOD_SYS_IO, 6), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; uint8_t channels; })                                                                           \
  X(ERR_IO_PWM_TIMERS_EXHAUSTED,   ERR_TAG(ERR_MOD_SYS_IO, 7), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; uint8_t timers; uint32_t frequency_Hz; }) /* a new frequency needs a timer and none is free */ \
  X(ERR_IO_BAD_READING,            ERR_TAG(ERR_MOD_SYS_IO, 8), ERR_SEV_HIGH, { uint16_t id; uint8_t pin; })               /* an ADC didn't return the channel asked for */

ERR_TAGS_DECLARE(ERR_SYS_IO_MAP)

/* Available modes to set IO to */
//#ref-enum @alias IO Mode
typedef enum sys_io_mode_e {
  SYS_IO_MODE_INPUT = 0,                     //@alias Input @description Digital input
  SYS_IO_MODE_INPUT_PULLUP = 1,              //@alias Input with Pullup @description Digital input with pull-up resistor
  SYS_IO_MODE_INPUT_PULLDOWN = 2,            //@alias Input with Pulldown @description Digital input with pull-down resistor
  SYS_IO_MODE_OUTPUT_PUSH_PULL = 3,          //@alias Output Push-Pull @description Digital output push-pull
  SYS_IO_MODE_OUTPUT_OPEN_DRAIN = 4,         //@alias Output Open-Drain @description Digital output open-drain
  SYS_IO_MODE_OUTPUT_OPEN_DRAIN_PULLUP = 5,  //@alias Output Open-Drain-Pullup @description Digital output open-drain with pull-up
  SYS_IO_MODE_PWM = 6,                       //@alias PWM @description PWM output
  SYS_IO_MODE_ADC = 7,                       //@alias ADC @description Analog ADC input
  SYS_IO_MODE_DAC = 8,                       //@alias DAC @description Analog DAC output
} sys_io_mode_e;

/* Available interrupt modes */
//#ref-enum @alias Interrupt Mode
typedef enum sys_io_intr_mode_e {
  SYS_IO_INTR_DISABLE = 0,             //@alias Disable @description Interrupt disabled
  SYS_IO_INTR_MODE_RISING_EDGE = 1,    //@alias Rising Edge @description Trigger on rising edge
  SYS_IO_INTR_MODE_FALLING_EDGE = 2,   //@alias Falling Edge @description Trigger on falling edge
  SYS_IO_INTR_MODE_BOTH_EDGES = 3,     //@alias Both Edges @description Trigger on both rising and falling edges
  SYS_IO_INTR_ADC_WINDOW_OUTSIDE = 4,  //@alias Outside set window @description Trigger when ADC leaves window
  SYS_IO_INTR_ADC_WINDOW_INSIDE = 5,   //@alias Inside set window @description Trigger when ADC enters window
} sys_io_intr_mode_e;

/* Events of the IO contract (sys_event.h "Events tables"), published by a
 * device for a pin armed with CONFIGURE_INTR; channel = pin. Flags 0: an edge
 * is code flow (an alert pin, the VM), not something to show by itself. */
#define SYS_IO_EVENTS(X)  \
  X(RISING,      0x00, 0) \
  X(FALLING,     0x01, 0) \
  X(ADC_OUTSIDE, 0x02, 0) \
  X(ADC_INSIDE,  0x03, 0)

#define SYS_IO__EV(name, n, flags) \
  IO_EV_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_IO, n), IO_EV_##name##_FLAGS = (flags),

enum { SYS_IO_EVENTS(SYS_IO__EV) };

typedef struct __attribute__((packed)) {
  uint8_t pin;  /**< Device pin index */
} io_pin_args_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;    /**< Device pin index */
  uint8_t level;  /**< 0 = Low, 1 = High */
} io_level_args_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;    /**< Device pin index */
  uint32_t duty;  /**< PWM duty, 0 to 4095 (12 bits) */
} io_duty_args_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;            /**< Device pin index */
  uint32_t frequency_Hz;  /**< PWM frequency in Hz */
} io_freq_args_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;   /**< Device pin index */
  uint8_t mode;  /**< sys_io_mode_e */
} io_mode_args_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;         /**< Device pin index */
  int32_t voltage_mV;  /**< Voltage in mV */
} io_voltage_args_t;

/**
 * @brief ADC window of the SYS_IO_INTR_ADC_WINDOW_* modes.
 */
typedef struct __attribute__((packed)) {
  uint16_t threshold_up_mV;          /**< Upper threshold in mV */
  uint16_t threshold_down_mV;        /**< Lower threshold in mV */
  uint16_t hysteresis_mV;            /**< Hysteresis in mV */
  uint16_t event_counter_threshold;  /**< Samples past threshold before event */
} sys_io_adc_window_t;

/**
 * @brief Interrupt config of one pin. It only arms the source; who gets the
 * pin's events is not part of the contract.
 */
typedef struct __attribute__((packed)) {
  uint8_t mode;              /**< sys_io_intr_mode_e */
  uint8_t debounce;          /**< 1 = debounce enabled, 0 = disabled */
  sys_io_adc_window_t adc;   /**< ADC window configuration */
} sys_io_intr_cfg_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;             /**< Device pin index */
  sys_io_intr_cfg_t cfg;   /**< Interrupt configuration */
} io_intr_args_t;

/**
 * @brief The contract, once: X(NAME, op number, key_t, value_t). The read takes key_t and answers value_t, the write
 * takes value_t (the mirror). From it come IO_OP_<NAME>, io_key_<NAME>_t, io_value_<NAME>_t, IO_READ(NAME) /
 * IO_WRITE(NAME) and IO_ENTRY(NAME), _ENTRY_R, _ENTRY_W (SYS_DEVICE.MD "Contract tables"). Sides: LEVEL RW, TOGGLE W,
 * PWM_DUTY W, PWM_FREQ W, MODE W, RESET W, INTR W, VOLTAGE RW. Numbers are never reused.
 */
#define SYS_IO_CONTRACT(X)                              \
  X(LEVEL,    0x00, io_pin_args_t,     io_level_args_t) \
  X(TOGGLE,   0x01, sys_device_none_t, io_pin_args_t)   \
  X(PWM_DUTY, 0x03, sys_device_none_t, io_duty_args_t)  \
  X(PWM_FREQ, 0x04, sys_device_none_t, io_freq_args_t)  \
  X(MODE,     0x05, sys_device_none_t, io_mode_args_t)  \
  X(RESET,    0x06, sys_device_none_t, io_pin_args_t)   \
  X(INTR,     0x07, sys_device_none_t, io_intr_args_t)  \
  X(VOLTAGE,  0x09, io_pin_args_t,     io_voltage_args_t)

#define SYS_IO__ID(name, n, key_t, value_t) IO_OP_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_IO, n),
#define SYS_IO__TYPES(name, n, key_t, value_t) \
  typedef key_t io_key_##name##_t;             \
  typedef value_t io_value_##name##_t;

enum { SYS_IO_CONTRACT(SYS_IO__ID) };
SYS_IO_CONTRACT(SYS_IO__TYPES)

#define IO_READ(name)    SYS_DEVICE_READ(io, name)
#define IO_WRITE(name)   SYS_DEVICE_WRITE(io, name)
#define IO_ENTRY(name)   SYS_DEVICE_ENTRY(IO_OP_##name, io, name)
#define IO_ENTRY_R(name) SYS_DEVICE_ENTRY_R(IO_OP_##name, io, name)
#define IO_ENTRY_W(name) SYS_DEVICE_ENTRY_W(IO_OP_##name, io, name)

/**
 * @brief Pin of a device. In a create struct a pin that is not used is
 * SYS_IO_PIN_NONE: a zero-filled field is device 0 / pin 0, a real pin.
 */
typedef struct __attribute__((packed)) {
  uint16_t device;  /**< Device ID (SYS_DEVICE_ID_NONE if unused) */
  uint8_t pin;      /**< Pin index on that device */
} sys_io_pin_t;

#define SYS_IO_PIN_NONE ((sys_io_pin_t){.device = SYS_IO_PIN_NONE_DEVICE, .pin = SYS_IO_PIN_NONE_INDEX})

//#ref-defines io_pin_none @alias Unused pin @prefix SYS_IO_PIN_NONE_
#define SYS_IO_PIN_NONE_DEVICE SYS_DEVICE_ID_NONE  //@alias Device @description The device of an unused pin: no pin is used when the device is this
#define SYS_IO_PIN_NONE_INDEX  0xFFu  //@alias Pin @description The pin index of an unused pin

static inline bool sys_io_pin_valid(sys_io_pin_t p) {
  return p.device != SYS_DEVICE_ID_NONE;
}

/** @brief A device's answer to a LEVEL / VOLTAGE read: the write's struct for the pin (the mirror). */
MUST_USE err_h sys_io_reply_level(uint8_t pin, bool level, uint8_t* out, uint32_t out_cap, uint32_t* out_len);
MUST_USE err_h sys_io_reply_voltage(uint8_t pin, int32_t voltage_mV, uint8_t* out, uint32_t out_cap, uint32_t* out_len);

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
