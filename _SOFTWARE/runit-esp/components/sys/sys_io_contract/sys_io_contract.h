#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sys_device.h"
#include "sys_event.h"
#include "sys_io_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The IO contract, 0x01xx (0x01 the contract, xx its op): the pins of a device, the same ops on every device
 * that has them (a PWM expander, a GPIO expander, the ESP's own pins). Not a module with state: a device implements the
 * ops it can in its table, an op it doesn't have answers ERR_DEV_UNKNOWN_OP. A device's pin is (device ID, pin); using
 * it is a call of that device's op, the same one a remote command makes. This header is the caller's side: the
 * contract, SYS_IO_WRITE / SYS_IO_READ (the call, typed by the contract table) and the wrappers with logic of their
 * own. A device driver (with pins or claiming another one's pin): sys_io_driver.h.
 *
 * LEVEL        - the digital level of a pin, set and read
 * TOGGLE       - inverts the level of an output pin
 * PWM_DUTY     - PWM duty of a pin, 0 .. IO_PWM_DUTY_MAX (12 bits); a device with another resolution scales or
 *                rejects with ERR_RANGE
 * PWM_FREQ     - PWM frequency of a pin in Hz
 * MODE         - sets the pin's mode (sys_io_mode_e)
 * RESET        - puts the pin back to its power-on state
 * INTR         - arms the pin's interrupt source (sys_io_intr_cfg_t); who gets the events is not part of the contract
 * VOLTAGE      - the voltage of an analog pin in mV: a DAC output set, an ADC input read
 * LEVELS       - the levels of many pins in one read: the pins read and the high ones, as masks
 * VOLTAGES     - the voltages of many pins in one read: a {pin, mV} row per pin read
 * AVAILABLE    - mask of the pins the device has
 * LOCKED       - mask of the pins that are locked
 * LOCK         - locks a pin for the device that depends on it (PCA9685's OE, a chip's RST, an I2C bus's SDA). Always,
 *                local
 * UNLOCK       - unlocks a pin. Always, local
 * Events       - RISING, FALLING, ADC_OUTSIDE, ADC_INSIDE (IO_EVENTS below)
 *
 * A locked pin has an owner, the device whose op called LOCK (sys_device_caller(); SYS_DEVICE_ID_NONE for no device).
 * The writes LEVEL, TOGGLE, PWM_*, MODE, RESET, INTR, VOLTAGE on it are the owner's alone: it uses the pin with the
 * same ops as before the lock, and anyone else (another device, the app, a VM program) gets ERR_DEV_PIN_LOCKED{owner}.
 * A pin no device owns (owner SYS_DEVICE_ID_NONE: the chip's own peripheral) refuses every write. Reads stay allowed.
 * LOCK and UNLOCK are LOCAL: only a driver's C reaches them (sys_io_claim), never the app or a VM program (the device
 * router refuses them, ERR_DEV_LOCAL_OP), and UNLOCK is the owner's too (ERR_DEV_PIN_NOT_OWNED{owner}), so a pin a
 * driver holds can't be taken from under it. Masks are u64 little-endian, bit n = pin n (a device has at most 64
 * pins). Payloads are packed little-endian structs.
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

/* ----------------------------------------------------------------------------
 * Frames
 * ------------------------------------------------------------------------- */

/** @brief TOGGLE / RESET / LOCK / UNLOCK args and the LEVEL / VOLTAGE read key: a pin */
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

/** @brief VOLTAGE args and answer; a row of VOLTAGES' answer */
typedef struct __attribute__((packed)) {
  uint8_t pin;         /**< Device pin index */
  int32_t voltage_mV;  /**< Voltage in mV */
} io_voltage_args_t;

/** @brief LEVELS / VOLTAGES read key: the pins to read */
typedef struct __attribute__((packed)) {
  uint64_t pins;  /**< Bit n: pin n */
} io_pins_args_t;

/** @brief LEVELS answer */
typedef struct __attribute__((packed)) {
  uint64_t read;  /**< Bit n: pin n was read (a pin asked for and not set up for a level is left out) */
  uint64_t high;  /**< Bit n: pin n is high */
} io_levels_t;

/** @brief ADC window of the SYS_IO_INTR_ADC_WINDOW_* modes. */
typedef struct __attribute__((packed)) {
  uint16_t threshold_up_mV;          /**< Upper threshold in mV */
  uint16_t threshold_down_mV;        /**< Lower threshold in mV */
  uint16_t hysteresis_mV;            /**< Hysteresis in mV */
  uint16_t event_counter_threshold;  /**< Samples past threshold before event */
} sys_io_adc_window_t;

/** @brief Interrupt config of one pin. It only arms the source; who gets the pin's events is not in the contract. */
typedef struct __attribute__((packed)) {
  uint8_t mode;             /**< sys_io_intr_mode_e */
  uint8_t debounce;         /**< 1 = debounce enabled, 0 = disabled */
  sys_io_adc_window_t adc;  /**< ADC window configuration */
} sys_io_intr_cfg_t;

/** @brief INTR args */
typedef struct __attribute__((packed)) {
  uint8_t pin;            /**< Device pin index */
  sys_io_intr_cfg_t cfg;  /**< Interrupt configuration */
} io_intr_args_t;

/* ----------------------------------------------------------------------------
 * The contract and its events
 * ------------------------------------------------------------------------- */

/**
 * @brief The contract, once: X(P, OP_NAME, op number, key_t, value_t, flags). The read takes key_t and answers value_t,
 * the write takes value_t (the mirror); VOLTAGES answers rows of its value_t. SYS_DEVICE_CONTRACT_DECLARE gives
 * CONTRACT_IO_<OP_NAME>, CONTRACT_IO_key_<OP_NAME>_t and CONTRACT_IO_value_<OP_NAME>_t; a driver writes
 * OP_DECLARE_READ(CONTRACT_IO, OP_NAME) / OP_DECLARE_WRITE(CONTRACT_IO, OP_NAME) and lists OP_ENTRY(CONTRACT_IO,
 * OP_NAME), OP_ENTRY_R, OP_ENTRY_W. Sides: LEVEL RW, TOGGLE W, PWM_DUTY W, PWM_FREQ W, MODE W, RESET W, INTR W, VOLTAGE
 * RW, AVAILABLE R, LOCKED R, LOCK W, UNLOCK W, LEVELS R, VOLTAGES R.
 */
#define CONTRACT_IO_OPERATIONS(X, P)                                                                         \
  X(P, OP_LEVEL,        0x00, io_pin_args_t,  io_level_args_t,   0)                                          \
  X(P, OP_TOGGLE,       0x01, cmd_none_t,     io_pin_args_t,     0)                                          \
  X(P, OP_PWM_DUTY,     0x02, cmd_none_t,     io_duty_args_t,    0)                                          \
  X(P, OP_PWM_FREQ,     0x03, cmd_none_t,     io_freq_args_t,    0)                                          \
  X(P, OP_MODE,         0x04, cmd_none_t,     io_mode_args_t,    0)                                          \
  X(P, OP_RESET,        0x05, cmd_none_t,     io_pin_args_t,     0)                                          \
  X(P, OP_INTR,         0x06, cmd_none_t,     io_intr_args_t,    0)                                          \
  X(P, OP_VOLTAGE,      0x07, io_pin_args_t,  io_voltage_args_t, 0)                                          \
  X(P, OP_AVAILABLE,    0x08, cmd_none_t,     uint64_t,          0)                                          \
  X(P, OP_LOCKED,       0x09, cmd_none_t,     uint64_t,          0)                                          \
  X(P, OP_LOCK,         0x0A, cmd_none_t,     io_pin_args_t,     SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_LOCAL) \
  X(P, OP_UNLOCK,       0x0B, cmd_none_t,     io_pin_args_t,     SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_LOCAL) \
  X(P, OP_LEVELS,       0x0C, io_pins_args_t, io_levels_t,       0)                                          \
  X(P, OP_VOLTAGES,     0x0D, io_pins_args_t, io_voltage_args_t, 0)

SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_IO, MODULE_ID_IO)

/* Events of the IO contract, published by a device for a pin armed with INTR: source = the device, channel = the pin.
 * Flags 0: an edge is code flow (an alert pin, the VM), not something to show by itself. */
#define IO_EVENTS(X, P)                       \
  X(P, RISING,      0x01, 0, {})              \
  X(P, FALLING,     0x02, 0, {})              \
  X(P, ADC_OUTSIDE, 0x03, 0, { int32_t mV; }) \
  X(P, ADC_INSIDE,  0x04, 0, { int32_t mV; })

SYS_EVENTS_DECLARE(IO, MODULE_ID_IO, DEVICE)

/* ----------------------------------------------------------------------------
 * A pin reference
 * ------------------------------------------------------------------------- */

/**
 * @brief Pin of a device. In a create struct a pin that is not used is SYS_IO_PIN_NONE: a zero-filled field is device 0
 * / pin 0, a real pin.
 */
typedef struct __attribute__((packed)) {
  uint8_t device;  /**< Device ID (SYS_DEVICE_ID_NONE if unused) */
  uint8_t pin;     /**< Pin index on that device */
} sys_io_pin_t;

#define SYS_IO_PIN_NONE ((sys_io_pin_t){.device = SYS_IO_PIN_NONE_DEVICE, .pin = SYS_IO_PIN_NONE_INDEX})

//#ref-defines io_pin_none @alias Unused pin @prefix SYS_IO_PIN_NONE_ @duplicates
#define SYS_IO_PIN_NONE_DEVICE SYS_DEVICE_ID_NONE  //@alias Device @description The device of an unused pin: no pin is used when the device is this
#define SYS_IO_PIN_NONE_INDEX  0xFFu               //@alias Pin @description The pin index of an unused pin

/**
 * @brief Whether a pin reference is used (not SYS_IO_PIN_NONE)
 * @param[in] p The pin
 * @return true when it names a device
 */
static inline bool sys_io_pin_valid(sys_io_pin_t p) {
  return p.device != SYS_DEVICE_ID_NONE;
}

/* ----------------------------------------------------------------------------
 * Calls
 * ------------------------------------------------------------------------- */

/**
 * @brief Writes op OP_NAME of the IO contract on pin `p`: its value_t from the contract table, the pin and the fields
 * given (none for OP_TOGGLE, OP_RESET, OP_LOCK, OP_UNLOCK). The compiler checks the field names against the op's frame.
 * @return ERR_DEV_UNKNOWN_OP if the device has no such op, ERR_DEV_PIN_LOCKED{owner} for a pin another device locked,
 *         ERR_DEV_PIN_NOT_OWNED{owner} for UNLOCK of one, ERR_RANGE, or the error of the device write
 * @usage ERR_TRY(SYS_IO_WRITE(pin, OP_LEVEL, .level = 1));
 *        ERR_TRY(SYS_IO_WRITE(pin, OP_PWM_DUTY, .duty = duty));
 *        ERR_TRY(SYS_IO_WRITE(pin, OP_INTR, .cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE}));
 *        ERR_TRY(SYS_IO_WRITE(pin, OP_LOCK));
 */
#define SYS_IO_WRITE(p, name, ...) SYS_DEVICE_SET((p).device, CONTRACT_IO, name, .pin = (p).pin, __VA_ARGS__)

/**
 * @brief Reads op OP_NAME of the IO contract on pin `p` into `*answer` (its value_t from the contract table, the pin
 * first).
 * @return As SYS_DEVICE_READ
 * @usage io_voltage_args_t v; ERR_TRY(SYS_IO_READ(pin, OP_VOLTAGE, &v));
 */
#define SYS_IO_READ(p, name, answer) SYS_DEVICE_GET((p).device, CONTRACT_IO, name, (answer), .pin = (p).pin)

/**
 * @brief MODE for a pin another driver uses: a device without MODE (fixed-function pins: PCA9685 PWM channels, DAC
 * outputs) is fine, any other failure is returned.
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
 * @brief Reads the level of a pin (op LEVEL); allowed on a locked pin
 * @param[in]  p     The pin
 * @param[out] level true: high
 * @return ERR_BAD_ARG for a NULL level, or the error of the device read
 */
MUST_USE err_h sys_io_get_level(sys_io_pin_t p, bool* level);

/**
 * @brief Reads the voltage of an analog pin (op VOLTAGE)
 * @param[in]  p      The pin
 * @param[out] out_mV Voltage in mV
 * @return ERR_BAD_ARG for a NULL out_mV, or the error of the device read
 */
MUST_USE err_h sys_io_get_voltage(sys_io_pin_t p, int32_t* out_mV);

#ifdef __cplusplus
}
#endif
