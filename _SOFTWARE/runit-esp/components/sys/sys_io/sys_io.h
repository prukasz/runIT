#pragma once

#include "sys_device.h"
#include "sys_io_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SYS_DEVICE_CONTRACT_IO: 0x01xx, where 0x01 is the IO contract and xx its op. The pins of a device, the same ops on
 * every device that has them (a PWM expander, a GPIO expander, the ESP's own pins). Not a module with state: a device
 * implements the ops it can in its table, the ops it has are its class, an op it doesn't have answers
 * ERR_DEV_UNKNOWN_OP. A device's pin is (device ID, pin); using it is a call of that device's op, the same one a remote
 * command makes. The wrappers below build that call; they add nothing else.
 * LEVEL    - the digital level of a pin, set and read
 * TOGGLE   - inverts the level of an output pin
 * PWM_DUTY - PWM duty of a pin, 0 .. IO_PWM_DUTY_MAX (12 bits); a device with another resolution scales or rejects
 *            with ERR_DEV_RANGE
 * PWM_FREQ - PWM frequency of a pin in Hz
 * MODE     - sets the pin's mode (sys_io_mode_e)
 * RESET    - puts the pin back to its power-on state
 * INTR     - arms the pin's interrupt source (sys_io_intr_cfg_t); who gets the events is not part of the contract
 * VOLTAGE  - the voltage of an analog pin in mV: a DAC output set, an ADC input read
 * Events   - RISING, FALLING, ADC_OUTSIDE, ADC_INSIDE (IO_EVENTS below)
 * Payloads are packed little-endian structs.
 */

#define IO_PWM_DUTY_MAX 4095u

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
#define IO_EVENTS(X, P)      \
  X(P, RISING,      0x00, 0) \
  X(P, FALLING,     0x01, 0) \
  X(P, ADC_OUTSIDE, 0x02, 0) \
  X(P, ADC_INSIDE,  0x03, 0)

/** @brief TOGGLE / RESET args and the LEVEL / VOLTAGE read key: a pin */
typedef struct __attribute__((packed)) {
  uint8_t pin;  /**< Device pin index */
} io_pin_args_t;

/** @brief LEVEL args and answer */
typedef struct __attribute__((packed)) {
  uint8_t pin;    /**< Device pin index */
  uint8_t level;  /**< 0 = Low, 1 = High */
} io_level_args_t;

/** @brief PWM_DUTY args */
typedef struct __attribute__((packed)) {
  uint8_t pin;    /**< Device pin index */
  uint32_t duty;  /**< PWM duty, 0 to 4095 (12 bits) */
} io_duty_args_t;

/** @brief PWM_FREQ args */
typedef struct __attribute__((packed)) {
  uint8_t pin;            /**< Device pin index */
  uint32_t frequency_Hz;  /**< PWM frequency in Hz */
} io_freq_args_t;

/** @brief MODE args */
typedef struct __attribute__((packed)) {
  uint8_t pin;   /**< Device pin index */
  uint8_t mode;  /**< sys_io_mode_e */
} io_mode_args_t;

/** @brief VOLTAGE args and answer */
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
 * @brief The contract, once: X(P, NAME, op number, key_t, value_t, flags). The read takes key_t and answers value_t,
 * the write takes value_t (the mirror). SYS_DEVICE_CONTRACT_DECLARE gives IO_OP_<NAME>, IO_key_<NAME>_t and
 * IO_value_<NAME>_t; a driver writes OP_READ(IO, NAME) / OP_WRITE(IO, NAME) and lists OP_ENTRY(IO, NAME), OP_ENTRY_R,
 * OP_ENTRY_W (SYS_DEVICE.MD "Contract tables"). Sides: LEVEL RW, TOGGLE W, PWM_DUTY W, PWM_FREQ W, MODE W, RESET W,
 * INTR W, VOLTAGE RW. Numbers are never reused.
 */
#define IO_OPERATIONS(X, P)                                          \
  X(P, LEVEL,    0x00, io_pin_args_t,          io_level_args_t,   0) \
  X(P, TOGGLE,   0x01, sys_device_args_none_t, io_pin_args_t,     0) \
  X(P, PWM_DUTY, 0x03, sys_device_args_none_t, io_duty_args_t,    0) \
  X(P, PWM_FREQ, 0x04, sys_device_args_none_t, io_freq_args_t,    0) \
  X(P, MODE,     0x05, sys_device_args_none_t, io_mode_args_t,    0) \
  X(P, RESET,    0x06, sys_device_args_none_t, io_pin_args_t,     0) \
  X(P, INTR,     0x07, sys_device_args_none_t, io_intr_args_t,    0) \
  X(P, VOLTAGE,  0x09, io_pin_args_t,          io_voltage_args_t, 0)

SYS_DEVICE_CONTRACT_DECLARE(IO, SYS_DEVICE_CONTRACT_IO)
SYS_EVENTS_DECLARE(IO)

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

/**
 * @brief Whether a pin reference is used (not SYS_IO_PIN_NONE)
 * @param[in] p The pin
 * @return true when it names a device
 */
static inline bool sys_io_pin_valid(sys_io_pin_t p) {
  return p.device != SYS_DEVICE_ID_NONE;
}

/**
 * @brief A device's answer to a LEVEL read: the write's struct for the pin (the mirror).
 * @param[in]  pin   Device pin index
 * @param[in]  level true: high
 * @param[out] out   The op's answer
 * @return The error of writing the answer
 */
MUST_USE err_h sys_io_reply_level(uint8_t pin, bool level, cmd_out_t* out);

/**
 * @brief A device's answer to a VOLTAGE read: the write's struct for the pin (the mirror).
 * @param[in]  pin        Device pin index
 * @param[in]  voltage_mV Voltage in mV
 * @param[out] out        The op's answer
 * @return The error of writing the answer
 */
MUST_USE err_h sys_io_reply_voltage(uint8_t pin, int32_t voltage_mV, cmd_out_t* out);

/***************************************************************
* C API
******************************************************************/

/**
 * @brief Sets the mode of a pin (op MODE)
 * @param[in] p    The pin
 * @param[in] mode sys_io_mode_e
 * @return ERR_DEV_UNKNOWN_OP if the device has no MODE, or the error of the device write
 */
MUST_USE err_h sys_io_set_mode(sys_io_pin_t p, sys_io_mode_e mode);

/**
 * @brief Puts a pin back to its power-on state (op RESET)
 * @param[in] p The pin
 * @return ERR_DEV_UNKNOWN_OP if the device has no RESET, or the error of the device write
 */
MUST_USE err_h sys_io_reset(sys_io_pin_t p);

/**
 * @brief SET_MODE for a pin another driver uses: a device without SET_MODE (fixed-function pins: PCA9685 PWM channels,
 * DAC outputs) is fine, any other failure is returned.
 * @param[in] p    The pin
 * @param[in] mode sys_io_mode_e
 * @return The error of the device write, except ERR_DEV_UNKNOWN_OP
 */
MUST_USE err_h sys_io_prepare(sys_io_pin_t p, sys_io_mode_e mode);

/**
 * @brief An open-drain alert / interrupt line as an input: with the pull-up where the device has one (ESP GPIO), else
 * plain (an expander such as the TCA6424A: the board has an external pull-up).
 * @param[in] p The pin
 * @return As sys_io_prepare
 */
MUST_USE err_h sys_io_prepare_input(sys_io_pin_t p);

/**
 * @brief RESET for a pin another driver used: same tolerance as sys_io_prepare.
 * @param[in] p The pin
 * @return The error of the device write, except ERR_DEV_UNKNOWN_OP
 */
MUST_USE err_h sys_io_release(sys_io_pin_t p);

/**
 * @brief Arms or disarms the interrupt source of a pin (op INTR)
 * @param[in] p   The pin
 * @param[in] cfg Mode, debounce and the ADC window
 * @return ERR_DEV_BAD_ARG for a NULL cfg, or the error of the device write
 */
MUST_USE err_h sys_io_configure_intr(sys_io_pin_t p, const sys_io_intr_cfg_t* cfg);

/**
 * @brief Sets the level of an output pin (op LEVEL, write)
 * @param[in] p     The pin
 * @param[in] level true: high
 * @return ERR_DEV_PIN_LOCKED for a locked pin, or the error of the device write
 */
MUST_USE err_h sys_io_set_level(sys_io_pin_t p, bool level);

/**
 * @brief Inverts the level of an output pin (op TOGGLE)
 * @param[in] p The pin
 * @return ERR_DEV_PIN_LOCKED for a locked pin, or the error of the device write
 */
MUST_USE err_h sys_io_toggle(sys_io_pin_t p);

/**
 * @brief Reads the level of a pin (op LEVEL, read); allowed on a locked pin
 * @param[in]  p     The pin
 * @param[out] level true: high
 * @return ERR_DEV_BAD_ARG for a NULL level, or the error of the device read
 */
MUST_USE err_h sys_io_get_level(sys_io_pin_t p, bool* level);

/**
 * @brief Reads the voltage of an analog pin (op VOLTAGE, read)
 * @param[in]  p      The pin
 * @param[out] out_mV Voltage in mV
 * @return ERR_DEV_BAD_ARG for a NULL out_mV, or the error of the device read
 */
MUST_USE err_h sys_io_get_voltage(sys_io_pin_t p, int32_t* out_mV);

/**
 * @brief Sets the voltage of a DAC pin (op VOLTAGE, write)
 * @param[in] p          The pin
 * @param[in] voltage_mV Voltage in mV
 * @return ERR_DEV_RANGE for a voltage the device can't reach, ERR_DEV_PIN_LOCKED for a locked pin, or the error of
 * the device write
 */
MUST_USE err_h sys_io_set_voltage(sys_io_pin_t p, int32_t voltage_mV);

/**
 * @brief Sets the PWM duty of a pin (op PWM_DUTY)
 * @param[in] p    The pin
 * @param[in] duty 0 .. IO_PWM_DUTY_MAX (12 bits)
 * @return ERR_DEV_RANGE for a duty above IO_PWM_DUTY_MAX, ERR_DEV_PIN_LOCKED for a locked pin, or the error of the
 * device write
 */
MUST_USE err_h sys_io_set_pwm_duty(sys_io_pin_t p, uint32_t duty);

/**
 * @brief Sets the PWM frequency of a pin (op PWM_FREQ)
 * @param[in] p            The pin
 * @param[in] frequency_Hz Frequency in Hz
 * @return ERR_DEV_RANGE for a frequency the device can't make, ERR_DEV_PIN_LOCKED for a locked pin, or the error of
 * the device write
 */
MUST_USE err_h sys_io_set_pwm_frequency(sys_io_pin_t p, uint32_t frequency_Hz);

#ifdef __cplusplus
}
#endif
