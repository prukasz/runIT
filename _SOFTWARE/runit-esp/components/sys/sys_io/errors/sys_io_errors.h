#pragma once

#include "errors.h"

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
