#pragma once

#include <stdbool.h>
#include <stdint.h>
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
  uint8_t pin;
} pins_pin_args_t;

typedef struct __attribute__((packed)) {
  uint8_t pin;
  uint8_t level;  // 0 / 1
} pins_level_args_t;

// clang-format off
#define SYS_PINS_CONTRACT(X)                                                                     \
  X(AVAILABLE,        0x00, sys_device_none_t)  /* -> u64 mask: the pins the device has */        \
  X(LOCKED,           0x01, sys_device_none_t)  /* -> u64 mask: the locked ones */                \
  X(LOCK,             0x02, pins_pin_args_t)    /* ERR_DEV_PIN_LOCKED if it already is */         \
  X(UNLOCK,           0x03, pins_pin_args_t)    /* an unlocked pin stays unlocked, no error */    \
  X(SET_LOCKED_LEVEL, 0x04, pins_level_args_t)  /* the owner drives its locked pin */
// clang-format on

#define SYS_PINS__ID(name, n, args_t) PINS_OP_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_PINS, n),
#define SYS_PINS__ARGS(name, n, args_t) typedef args_t pins_args_##name##_t;

enum { SYS_PINS_CONTRACT(SYS_PINS__ID) };
SYS_PINS_CONTRACT(SYS_PINS__ARGS)

#define PINS_HANDLER(name) SYS_DEVICE_HANDLER(pins_h_##name, pins_args_##name##_t)
#define PINS_ENTRY(name) {PINS_OP_##name, pins_h_##name}

/* ----------------------------------------------------------------------------
 * For the device: the pin state and its checks. The device keeps one
 * sys_pins_state_t, calls sys_pins_state_check_free() before every write, and
 * answers the PINS ops with a line each:
 *   PINS_HANDLER(AVAILABLE)  { return sys_pins_reply(out, out_cap, out_len, ((my_t*)ctx)->pins.available); }
 *   PINS_HANDLER(LOCKED)     { return sys_pins_reply(out, out_cap, out_len, ((my_t*)ctx)->pins.locked); }
 *   PINS_HANDLER(LOCK)       { my_t* c = ctx; return sys_pins_state_lock(&c->pins, c->id, a->pin); }
 *   PINS_HANDLER(UNLOCK)     { my_t* c = ctx; return sys_pins_state_unlock(&c->pins, c->id, a->pin); }
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

/* ----------------------------------------------------------------------------
 * For everyone else: wrappers over sys_device_call
 * ------------------------------------------------------------------------- */

MUST_USE err_h sys_pins_available(uint16_t device, uint64_t* mask);
MUST_USE err_h sys_pins_locked(uint16_t device, uint64_t* mask);
MUST_USE err_h sys_pins_lock(sys_io_pin_t p);
MUST_USE err_h sys_pins_unlock(sys_io_pin_t p);
/** @brief The owner drives its locked pin. */
MUST_USE err_h sys_pins_set_locked_level(sys_io_pin_t p, bool level);

#ifdef __cplusplus
}
#endif
