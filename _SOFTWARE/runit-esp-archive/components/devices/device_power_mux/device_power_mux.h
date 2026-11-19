#pragma once

#include "sys_device.h"
#include "sys_io.h"
#include "sys_power_supply.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POWER_MUX_MIN_SWITCH_DELAY_MS 1u
#define POWER_MUX_MAX_SWITCH_DELAY_MS 100u

/**
 * @brief Power mux configuration struct (wire format for CREATE).
 */
typedef struct __attribute__((packed)) {
  sys_io_pin_t en_a_pin;    /**< Source A enable control pin */
  sys_io_pin_t en_b_pin;    /**< Source B enable control pin */
  uint8_t active_level;     /**< 1 = active high, 0 = active low */
  uint8_t switch_delay_ms;  /**< Break-before-make delay in ms */
} power_mux_cfg_t;

#ifdef __cplusplus
}
#endif
