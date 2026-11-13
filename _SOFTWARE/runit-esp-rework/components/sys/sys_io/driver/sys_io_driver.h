#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sys_device_driver.h"
#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief For a device with pins (an expander, the ESP's own GPIO): the pin state it keeps, its checks, and the lock
 * ops, which are the same on every such device. The device keeps one sys_io_pins_t and calls sys_io_pins_check_free()
 * before every write of the contract (the registry has refused it already while the device is suspended): it lets the
 * write through on a pin that is not locked and on one the caller of the op locked, so the owner of a pin uses it with
 * the contract's ordinary ops. A read handler calls sys_io_pins_check() and answers with CMD_REPLY
 * (sys_device_driver.h). The caller's side is sys_io.h.
 */

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
 * @brief AVAILABLE, LOCKED, LOCK and UNLOCK of a device whose struct (SYS_DEVICE_CTX) has the members `id` (its device
 * ID) and `pins` (sys_io_pins_t): they only read or change that state, so they are the same on every such device.
 * SYS_IO_PINS_ENTRIES are their op table rows.
 * @usage SYS_IO_PINS_HANDLERS
 *        static const sys_device_op_t k_ops[] = {..., SYS_IO_PINS_ENTRIES};
 */
#define SYS_IO_PINS_HANDLERS                                    \
  OP_READ(IO, AVAILABLE) {                                      \
    return CMD_REPLY(IO, AVAILABLE, c->pins.available);         \
  }                                                             \
  OP_READ(IO, LOCKED) {                                         \
    return CMD_REPLY(IO, LOCKED, c->pins.locked);               \
  }                                                             \
  OP_WRITE(IO, LOCK) {                                          \
    return sys_io_pins_lock(&c->pins, c->id, args->pin);        \
  }                                                             \
  OP_WRITE(IO, UNLOCK) {                                        \
    return sys_io_pins_unlock(&c->pins, c->id, args->pin);      \
  }
#define SYS_IO_PINS_ENTRIES \
  OP_ENTRY_R(IO, AVAILABLE), OP_ENTRY_R(IO, LOCKED), OP_ENTRY_W(IO, LOCK), OP_ENTRY_W(IO, UNLOCK)

#ifdef __cplusplus
}
#endif
