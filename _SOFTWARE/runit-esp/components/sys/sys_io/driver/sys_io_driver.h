#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sys_device_driver.h"
#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The driver side of IO (analogous to sys_device_driver.h): both for a device with pins (its sys_io_pins_t
 * state, checks, and lock ops) and for a device that claims another device's pin (sys_io_claim_t, take/drive, and
 * automatic release as a resource of its owner).
 *
 * A device with pins keeps one sys_io_pins_t and calls sys_io_pins_check_free() before every write of the contract
 * (the registry has refused it already while the device is suspended): it lets the write through on a pin that is not
 * locked and on one the caller of the op locked, so the owner of a pin uses it with the contract's ordinary ops. A
 * read handler calls sys_io_pins_check() and answers with CMD_REPLY (sys_device_driver.h).
 *
 * A device that uses another device's pin (a chip's RST or OE, an alert line) calls sys_io_claim*() from its create()
 * (its slot locked). A zeroed sys_io_claim_t is an unused pin (the registry allocates the device zeroed). The take
 * functions do nothing for SYS_IO_PIN_NONE, so an optional pin needs no check at the call site. Take: declare the use,
 * mode, level or interrupt, lock. The taken pin is a resource of its owner (sys_device_res_add): the registry releases
 * it when the owner is removed or its create() fails (unsubscribe, unlock, reset; not the reset with `keep`), so a
 * driver never releases a pin itself.
 *
 * The caller's side (contract ops, wrappers, events) is sys_io.h.
 */

/* ---- Pin state of a device with pins ---- */

#define SYS_IO_PINS_MAX 64u  // a mask has one bit per pin

/** @brief The pin state a device keeps: which pins exist, which are locked and by whom */
typedef struct {
  uint64_t available;              /**< Bit n: pin n exists */
  uint64_t locked;                 /**< Bit n: pin n is locked */
  uint8_t owner[SYS_IO_PINS_MAX];  /**< Of a locked pin: the device that locked it (sys_device_caller() of its LOCK),
                                        SYS_DEVICE_ID_NONE for no device (the chip's own peripheral, a module) */
} sys_io_pins_t;

/**
 * @brief Pins 0 .. count-1 exist, none is locked.
 * @param[out] s     The state to fill
 * @param[in]  count Number of pins, at most 64
 */
void sys_io_pins_init(sys_io_pins_t* s, uint8_t count);

/**
 * @brief Checks that a pin exists; a read of a locked pin is fine.
 * @param[in] s   The device's state
 * @param[in] dev Device ID, for the error
 * @param[in] pin Pin index
 * @return ERR_DEV_BAD_PIN unless the pin exists
 */
MUST_USE err_h sys_io_pins_check(const sys_io_pins_t* s, uint8_t dev, uint8_t pin);

/**
 * @brief Before a write: checks that a pin exists and that the caller of this op (sys_device_caller()) may write it:
 * the pin is not locked, or the caller is the device that locked it. A pin locked for no device
 * (SYS_DEVICE_ID_NONE) is nobody's to write.
 * @param[in] s   The device's state
 * @param[in] dev Device ID, for the error
 * @param[in] pin Pin index
 * @return ERR_DEV_BAD_PIN unless the pin exists, ERR_DEV_PIN_LOCKED{owner} if another owner has it
 */
MUST_USE err_h sys_io_pins_check_free(const sys_io_pins_t* s, uint8_t dev, uint8_t pin);

/**
 * @brief Locks a pin in the state for the caller of this op, sys_device_caller() (the LOCK op's body)
 * @param[in,out] s   The device's state
 * @param[in]     dev Device ID, for the error
 * @param[in]     pin Pin index
 * @return ERR_DEV_BAD_PIN unless the pin exists, ERR_DEV_PIN_LOCKED{owner} if it is locked already (by the caller
 *         too: a lock is taken once)
 */
MUST_USE err_h sys_io_pins_lock(sys_io_pins_t* s, uint8_t dev, uint8_t pin);

/**
 * @brief Unlocks a pin the caller of this op locked (the UNLOCK op's body); a pin that is not locked stays so.
 * @param[in,out] s   The device's state
 * @param[in]     dev Device ID, for the error
 * @param[in]     pin Pin index
 * @return ERR_DEV_BAD_PIN unless the pin exists, ERR_DEV_PIN_NOT_OWNED{owner} when another caller locked it
 */
MUST_USE err_h sys_io_pins_unlock(sys_io_pins_t* s, uint8_t dev, uint8_t pin);

/**
 * @brief AVAILABLE, LOCKED, LOCK and UNLOCK of a device whose struct (SYS_DEVICE_CTX) has the member `pins`
 * (sys_io_pins_t): they only read or change that state, so they are the same on every such device. SYS_IO_PINS_ENTRIES
 * are their op table rows.
 * @usage SYS_IO_PINS_HANDLERS
 *        static const sys_device_op_t k_ops[] = {..., SYS_IO_PINS_ENTRIES};
 */
#define SYS_IO_PINS_HANDLERS                                             \
  OP_DECLARE_READ(CONTRACT_IO, OP_AVAILABLE) {                           \
    return CMD_REPLY(CONTRACT_IO, OP_AVAILABLE, c->pins.available);      \
  }                                                                      \
  OP_DECLARE_READ(CONTRACT_IO, OP_LOCKED) {                              \
    return CMD_REPLY(CONTRACT_IO, OP_LOCKED, c->pins.locked);            \
  }                                                                      \
  OP_DECLARE_WRITE(CONTRACT_IO, OP_LOCK) {                               \
    return sys_io_pins_lock(&c->pins, c->id, args->pin);                 \
  }                                                                      \
  OP_DECLARE_WRITE(CONTRACT_IO, OP_UNLOCK) {                             \
    return sys_io_pins_unlock(&c->pins, c->id, args->pin);               \
  }
#define SYS_IO_PINS_ENTRIES                                                     \
  OP_ENTRY_R(CONTRACT_IO, OP_AVAILABLE), OP_ENTRY_R(CONTRACT_IO, OP_LOCKED),   \
  OP_ENTRY_W(CONTRACT_IO, OP_LOCK),      OP_ENTRY_W(CONTRACT_IO, OP_UNLOCK)

/* ---- Claiming another device's pin ---- */

/** @brief A pin a device took: what it did to it, so the release undoes exactly that */
typedef struct {
  sys_device_res_t res;    /**< The pin as a resource of its owner: the registry's */
  uint16_t sub;            /**< sys_event handle, while subscribed */
  sys_io_pin_t pin;        /**< The pin */
  uint8_t used : 1;        /**< Its mode is ours: release resets it */
  uint8_t locked : 1;      /**< Locked on its device: only its owner writes it (the IO ops, sys_io_claim_drive) */
  uint8_t subscribed : 1;  /**< Its events reach the owner's subscribed op */
  uint8_t keep : 1;        /**< Release leaves it as it is (an output the chip needs at a level): set after the take */
} sys_io_claim_t;

/**
 * @brief A pin in `mode`, not locked: its owner writes it with the IO ops (a PWM duty, a DAC voltage, an ADC pin's
 * thresholds). Its event `event` (0 = no subscription) reaches the owner's own op `op` (`LOCAL`, `TAIL` with
 * `sys_event_t`, `ALWAYS` if it should run while suspended). A device without MODE (fixed-function pins: PCA9685
 * channels, DAC outputs) is fine.
 * @param[in]  owner The owner's device ID
 * @param[out] p     What was done to the pin
 * @param[in]  pin   The pin; SYS_IO_PIN_NONE: nothing is done
 * @param[in]  mode  sys_io_mode_e
 * @param[in]  event Event tag (IO_EV_*) to subscribe the owner to, 0: none
 * @param[in]  op    Owner's own op receiving the event (ignored when `event` is 0)
 * @return ERR_DEV_NOT_FOUND{pin's device}, the error of the pin's device, or of the subscription
 */
MUST_USE err_h sys_io_claim(uint8_t owner, sys_io_claim_t* p, sys_io_pin_t pin, sys_io_mode_e mode, uint32_t event,
                            uint16_t op);

/**
 * @brief An output at `level`, locked for its owner.
 * @param[in]  owner The owner's device ID
 * @param[out] p     What was done to the pin
 * @param[in]  pin   The pin; SYS_IO_PIN_NONE: nothing is done
 * @param[in]  level Initial level, true: high
 * @return The error of the pin's device
 */
MUST_USE err_h sys_io_claim_output(uint8_t owner, sys_io_claim_t* p, sys_io_pin_t pin, bool level);

/**
 * @brief An input watched in `mode` (sys_io_intr_mode_e); its edges raise `event` (0 = no subscription) to the owner's
 * own op `op`. Locked.
 * @param[in]  owner The owner's device ID
 * @param[out] p     What was done to the pin
 * @param[in]  pin   The pin; SYS_IO_PIN_NONE: nothing is done
 * @param[in]  event Event tag (IO_EV_*) to subscribe the owner to, 0: none
 * @param[in]  op    Owner's own op receiving the event (ignored when `event` is 0)
 * @param[in]  mode  sys_io_intr_mode_e
 * @return The error of the pin's device, or of the subscription
 */
MUST_USE err_h sys_io_claim_input(uint8_t owner, sys_io_claim_t* p, sys_io_pin_t pin, uint32_t event, uint16_t op,
                                  uint8_t mode);

/**
 * @brief A pin a peripheral of the pin's own chip drives (the ESP32-S3's I2C or SPI controller through its GPIO
 * matrix): declared and locked, its mode left to the peripheral. The IO writes of anyone else are refused.
 * @param[in]  owner The owner's device ID
 * @param[out] p     What was done to the pin
 * @param[in]  pin   The pin; SYS_IO_PIN_NONE: nothing is done
 * @return ERR_DEV_NOT_FOUND{pin's device}, ERR_DEV_PIN_LOCKED when another owner has it, the error of the pin's device
 */
MUST_USE err_h sys_io_claim_peripheral(uint8_t owner, sys_io_claim_t* p, sys_io_pin_t pin);

/**
 * @brief Drives a pin its owner locked to `level` (the IO contract's LEVEL, which the pin's device lets its owner
 * write); a pin that is not locked is left alone, so an optional pin (SYS_IO_PIN_NONE) needs no check at the call
 * site. Called from an op of the owner (its create(), suspend() and resume() too). Any other write of a taken pin (an
 * interrupt mode, a PWM duty) is the IO op itself: SYS_IO_WRITE(p->pin, OP_INTR, ...).
 * @param[in] p     The taken pin
 * @param[in] level true: high
 * @return The error of the pin's device
 */
MUST_USE err_h sys_io_claim_drive(const sys_io_claim_t* p, bool level);

#ifdef __cplusplus
}
#endif
