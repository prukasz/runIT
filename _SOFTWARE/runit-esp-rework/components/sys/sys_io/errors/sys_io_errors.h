#pragma once

#include "errors.h"
#include "sys_io_id.h"

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_IO): what a device with pins reports besides the
 * shared ones in sys_device.h (BAD_PIN, PIN_LOCKED, RANGE, SUSPENDED).
 * ------------------------------------------------------------------------- */

#define ERR_SYS_IO_MAP(X, M)                                                                                                                                                     \
  X(M, ERR_IO_PIN_IN_USE,             1, ERR_SEV_HIGH, { uint8_t id; uint8_t pin; uint8_t mode; }) /* SET_MODE of a configured pin; mode = the current one */                    \
  X(M, ERR_IO_PIN_UNCONFIGURED,       2, ERR_SEV_HIGH, { uint8_t id; uint8_t pin; }) /* the op needs SET_MODE first */                                                           \
  X(M, ERR_IO_WRONG_MODE,             3, ERR_SEV_HIGH, { uint8_t id; uint8_t pin; uint8_t mode; }) /* the op doesn't fit the pin's current mode */                               \
  X(M, ERR_IO_MODE_UNSUPPORTED,       4, ERR_SEV_HIGH, { uint8_t id; uint8_t pin; uint8_t mode; }) /* this pin / device can't do the mode */                                     \
  X(M, ERR_IO_FEATURE_UNSUPPORTED,    5, ERR_SEV_HIGH, { uint8_t id; uint8_t pin; }) /* e.g. ADC on a pin without ADC */                                                         \
  X(M, ERR_IO_PWM_CHANNELS_EXHAUSTED, 6, ERR_SEV_HIGH, { uint8_t id; uint8_t pin; uint8_t channels; })                                                                           \
  X(M, ERR_IO_PWM_TIMERS_EXHAUSTED,   7, ERR_SEV_HIGH, { uint8_t id; uint8_t pin; uint8_t timers; uint32_t frequency_Hz; }) /* a new frequency needs a timer and none is free */ \
  X(M, ERR_IO_BAD_READING,            8, ERR_SEV_HIGH, { uint8_t id; uint8_t pin; }) /* an ADC didn't return the channel asked for */

ERR_TAGS_DECLARE(MODULE_ID_IO, ERR_SYS_IO_MAP)
