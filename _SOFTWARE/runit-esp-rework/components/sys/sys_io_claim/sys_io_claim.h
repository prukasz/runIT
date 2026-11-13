#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sys_device_driver.h"  // sys_device_res_t: a taken pin is a resource of its owner
#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief For a device that uses another device's pin (a chip's RST or OE, an alert line): what it did to each one, so
 * the release undoes exactly that. Called from the owner's create() (its slot locked). A zeroed struct is an unused pin
 * (the registry allocates the device zeroed). The take functions do nothing for SYS_IO_PIN_NONE, so an optional pin
 * needs no check at the call site. Take: declare the use, mode, level or interrupt, lock. The taken pin is a resource
 * of its owner (sys_device_res_add): the registry releases it when the owner is removed or its create() fails
 * (unsubscribe, unlock, reset; not the reset with `keep`), so a driver never releases a pin itself.
 */

/** @brief A pin a device took: what it did to it, so the release undoes exactly that */
typedef struct {
  sys_device_res_t res;    /**< The pin as a resource of its owner: the registry's */
  uint16_t sub;            /**< sys_event handle, while subscribed */
  sys_io_pin_t pin;        /**< The pin */
  uint8_t used : 1;        /**< Its mode is ours: release resets it */
  uint8_t locked : 1;      /**< Locked on its device: only its owner writes it (the IO ops, sys_io_claim_drive) */
  uint8_t subscribed : 1;  /**< Its events reach the owner's ON_EVENT */
  uint8_t keep : 1;        /**< Release leaves it as it is (an output the chip needs at a level): set after the take */
} sys_io_claim_t;

/**
 * @brief A pin in `mode`, not locked: its owner writes it with the IO ops (a PWM duty, a DAC voltage, an ADC pin's
 * thresholds). Its event `event` reaches the owner's ON_EVENT (0 = no subscription). A device without MODE
 * (fixed-function pins: PCA9685 channels, DAC outputs) is fine.
 * @param[in]  owner The owner's device ID
 * @param[out] p     What was done to the pin
 * @param[in]  pin   The pin; SYS_IO_PIN_NONE: nothing is done
 * @param[in]  mode  sys_io_mode_e
 * @param[in]  event Event tag (IO_EV_*) to subscribe the owner to, 0: none
 * @return ERR_DEV_NOT_FOUND{pin's device}, the error of the pin's device, or of the subscription
 */
MUST_USE err_h sys_io_claim(uint8_t owner, sys_io_claim_t* p, sys_io_pin_t pin, sys_io_mode_e mode, uint32_t event);

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
 * @brief An input watched in `mode` (sys_io_intr_mode_e); its edges raise `event` to the owner (0 = no subscription).
 * Locked.
 * @param[in]  owner The owner's device ID
 * @param[out] p     What was done to the pin
 * @param[in]  pin   The pin; SYS_IO_PIN_NONE: nothing is done
 * @param[in]  event Event tag (IO_EV_*) to subscribe the owner to, 0: none
 * @param[in]  mode  sys_io_intr_mode_e
 * @return The error of the pin's device, or of the subscription
 */
MUST_USE err_h sys_io_claim_input(uint8_t owner, sys_io_claim_t* p, sys_io_pin_t pin, uint32_t event, uint8_t mode);

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
 * interrupt mode, a PWM duty) is the IO op itself: SYS_IO_WRITE(p->pin, INTR, ...).
 * @param[in] p     The taken pin
 * @param[in] level true: high
 * @return The error of the pin's device
 */
MUST_USE err_h sys_io_claim_drive(const sys_io_claim_t* p, bool level);

#ifdef __cplusplus
}
#endif
