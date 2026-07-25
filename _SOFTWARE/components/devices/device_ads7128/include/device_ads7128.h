#pragma once
#include "sys_error.h"
#include "sys_io.h"

/**
 * @brief Configuration for ADS7128 ADC device.
 *
 * The eight channels are exposed as ADC pins 0..7 of the IO contract:
 * sys_io_get_voltage() reads one, sys_io_configure_intr() arms the on-chip
 * window comparator with SYS_IO_INTR_ADC_WINDOW_INSIDE / _OUTSIDE.
 */
typedef struct d_ads7128_cfg_t {
  uint8_t device_id; /**< MUST be first member */
  bool i2c_bus;
  uint8_t i2c_addr;
  /**< ALERT output of the chip; open-drain and active low, so the referenced pin
       wants SYS_IO_MODE_INPUT_PULLUP. Without it the window comparator still
       works, but nothing reports the events. */
  sys_io_pin_ref_t intr_pin;
  uint32_t vref_mv; /**< AVDD, which doubles as the ADC reference. Must be set. */
} d_ads7128_cfg_t;

/**
 * @brief Initialize and register the ADS7128 ADC device.
 *
 * @param cfg Pointer to device configuration struct
 * @return err_h Status of the registration
 */
err_h d_ads7128_create(const d_ads7128_cfg_t* cfg);
