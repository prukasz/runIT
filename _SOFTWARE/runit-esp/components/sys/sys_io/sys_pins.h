#pragma once

#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The PINS contract (SYS_DEVICE_CONTRACT_PINS: 0x02xx): which pins a
 * device has and which of them are locked. A pin another device depends on
 * (PCA9685's OE, a chip's RST) is locked by that device, so nothing else
 * writes it; the owner drives it with SET_LOCKED_LEVEL. A device with pins
 * and no locks still answers AVAILABLE and LOCKED, so the app can ask instead
 * of probing.
 *
 * Masks are u64 little-endian, bit n = pin n (a device has at most 64 pins).
 * A lock is bookkeeping, not an identity: whoever calls LOCK / UNLOCK is
 * trusted to be the owner. A locked pin refuses the IO contract's writes with
 * ERR_DEV_PIN_LOCKED; reads stay allowed.
 */

typedef struct __attribute__((packed)) {
  uint8_t pin;  /**< Device pin index */
} pins_pin_args_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;    /**< Device pin index */
  uint8_t level;  /**< 0 = Low, 1 = High */
} pins_level_args_t;

/**
 * @brief The contract, once: X(P, NAME, op number, key_t, value_t). The read takes key_t and answers value_t, the write
 * takes value_t (the mirror). SYS_DEVICE_CONTRACT_DECLARE gives PINS_OP_<NAME>, PINS_key_<NAME>_t and
 * PINS_value_<NAME>_t; a driver writes OP_READ(PINS, NAME) / OP_WRITE(PINS, NAME) and lists OP_ENTRY(PINS, NAME),
 * OP_ENTRY_R, OP_ENTRY_W (SYS_DEVICE.MD "Contract tables"). Sides: AVAILABLE R, LOCKED R, LOCK W, UNLOCK W,
 * LOCKED_LEVEL W. Numbers are never reused.
 */
#define PINS_OPERATIONS(X, P)                                    \
  X(P, AVAILABLE,    0x00, sys_device_none_t, uint64_t)          \
  X(P, LOCKED,       0x01, sys_device_none_t, uint64_t)          \
  X(P, LOCK,         0x02, sys_device_none_t, pins_pin_args_t)   \
  X(P, UNLOCK,       0x03, sys_device_none_t, pins_pin_args_t)   \
  X(P, LOCKED_LEVEL, 0x04, sys_device_none_t, pins_level_args_t)

SYS_DEVICE_CONTRACT_DECLARE(PINS, SYS_DEVICE_CONTRACT_PINS)

/* ----------------------------------------------------------------------------
 * For the device: the pin state and its checks. The device keeps one sys_pins_state_t, calls
 * sys_pins_state_check_free() before every write (the registry has refused it already while the device is
 * suspended), and answers the PINS ops with
 * SYS_PINS_STATE_HANDLERS below.
 * ------------------------------------------------------------------------- */

typedef struct {
  uint64_t available;  // bit n: pin n exists
  uint64_t locked;     // bit n: pin n is locked
} sys_pins_state_t;

/** @brief Pins 0 .. count-1 exist, none is locked. count <= 64. */
void sys_pins_state_init(sys_pins_state_t* s, uint8_t count);

/** @brief ERR_DEV_BAD_PIN unless the pin exists. A read of a locked pin is fine. */
MUST_USE err_h sys_pins_state_check(const sys_pins_state_t* s, uint16_t dev_id, uint8_t pin);

/** @brief Before a write: ERR_DEV_BAD_PIN unless the pin exists, ERR_DEV_PIN_LOCKED if it is locked. */
MUST_USE err_h sys_pins_state_check_free(const sys_pins_state_t* s, uint16_t dev_id, uint8_t pin);

MUST_USE err_h sys_pins_state_lock(sys_pins_state_t* s, uint16_t dev_id, uint8_t pin);
MUST_USE err_h sys_pins_state_unlock(sys_pins_state_t* s, uint16_t dev_id, uint8_t pin);

/** @brief The answer of AVAILABLE / LOCKED. */
MUST_USE err_h sys_pins_reply(uint8_t* out, uint32_t out_cap, uint32_t* out_len, uint64_t mask);

/**
 * @brief AVAILABLE, LOCKED, LOCK and UNLOCK of a device whose struct has the members `base` (sys_device_base_t) and
 * `pins` (sys_pins_state_t): they only read or change that state, so they are the same on every such device.
 * SYS_PINS_STATE_ENTRIES are their op table rows. LOCKED_LEVEL writes the pin, so it stays the device's own.
 * @usage SYS_PINS_STATE_HANDLERS(pca_t)
 *        static const sys_device_op_t k_ops[] = {..., SYS_PINS_STATE_ENTRIES, OP_ENTRY_W(PINS, LOCKED_LEVEL)};
 */
#define SYS_PINS_STATE_HANDLERS(ctx_t)                                                                            \
  OP_READ(PINS, AVAILABLE) { return sys_pins_reply(out, out_cap, out_len, ((const ctx_t*)ctx)->pins.available); } \
  OP_READ(PINS, LOCKED) { return sys_pins_reply(out, out_cap, out_len, ((const ctx_t*)ctx)->pins.locked); }       \
  OP_WRITE(PINS, LOCK) {                                                                                          \
    ctx_t* c = ctx;                                                                                               \
    return sys_pins_state_lock(&c->pins, c->base.id, args->pin);                                                  \
  }                                                                                                               \
  OP_WRITE(PINS, UNLOCK) {                                                                                        \
    ctx_t* c = ctx;                                                                                               \
    return sys_pins_state_unlock(&c->pins, c->base.id, args->pin);                                                \
  }
#define SYS_PINS_STATE_ENTRIES \
  OP_ENTRY_R(PINS, AVAILABLE), OP_ENTRY_R(PINS, LOCKED), OP_ENTRY_W_ALWAYS(PINS, LOCK), OP_ENTRY_W_ALWAYS(PINS, UNLOCK)

/* ----------------------------------------------------------------------------
 * For everyone else: wrappers over sys_device_call
 * ------------------------------------------------------------------------- */

MUST_USE err_h sys_pins_available(uint16_t device, uint64_t* mask);
MUST_USE err_h sys_pins_locked(uint16_t device, uint64_t* mask);
MUST_USE err_h sys_pins_lock(sys_io_pin_t p);
MUST_USE err_h sys_pins_unlock(sys_io_pin_t p);
/** @brief The owner drives its locked pin. */
MUST_USE err_h sys_pins_set_locked_level(sys_io_pin_t p, bool level);

/* ----------------------------------------------------------------------------
 * A device's own pins: what it did to each one, so its teardown undoes exactly that. A zeroed struct is an unused pin
 * (the registry allocates the device zeroed). The take functions do nothing for SYS_IO_PIN_NONE, so an optional pin needs
 * no check at the call site. Take: use, mode, level or interrupt, lock. Release: unsubscribe, unlock, reset (SYS_IO.MD
 * "A device's own pins").
 * ------------------------------------------------------------------------- */
typedef struct {
  sys_io_pin_t pin;
  uint8_t sub;      // sys_event handle, while subscribed
  bool used;        // its mode is ours: release resets it
  bool locked;      // locked on its device, driven by its owner (sys_pins_drive)
  bool subscribed;  // its edges reach the owner's ON_EVENT
  bool keep;        // release leaves it as it is (an output the chip needs at a level): set after the take
} sys_pins_owned_t;

/**
 * @brief A pin in `mode`, not locked: its owner writes it with the IO ops (a PWM duty, a DAC voltage, an ADC pin's
 * thresholds). Its events `event` reach the owner's ON_EVENT (0 = no subscription). A device without SET_MODE
 * (fixed-function pins: PCA9685 channels, DAC outputs) is fine.
 */
MUST_USE err_h sys_pins_take(sys_device_base_t* dev, sys_pins_owned_t* p, sys_io_pin_t pin, sys_io_mode_e mode,
                             uint16_t event);

/** @brief An output at `level`, locked for its owner. */
MUST_USE err_h sys_pins_take_output(sys_device_base_t* dev, sys_pins_owned_t* p, sys_io_pin_t pin, bool level);

/**
 * @brief An input watched in `mode` (sys_io_intr_mode_e); its edges raise `event` to the owner (0 = no subscription).
 * Locked.
 */
MUST_USE err_h sys_pins_take_input(sys_device_base_t* dev, sys_pins_owned_t* p, sys_io_pin_t pin, uint16_t event,
                                   uint8_t mode);

/** @brief Drives a pin its owner locked to `level`; a pin that is not locked is left alone. */
MUST_USE err_h sys_pins_drive(const sys_pins_owned_t* p, bool level);

/** @brief Undoes the take: unsubscribe, unlock, reset (not with `keep`). Safe on an unused pin. */
MUST_USE err_h sys_pins_release(sys_pins_owned_t* p);

#ifdef __cplusplus
}
#endif
