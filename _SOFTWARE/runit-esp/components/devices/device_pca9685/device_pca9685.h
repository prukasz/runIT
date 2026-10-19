#pragma once

#include "sys_device.h"
#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCA9685_CHANNELS 16u
#define PCA9685_MIN_FREQUENCY_HZ 24u    // prescaler 255 at the 25 MHz oscillator
#define PCA9685_MAX_FREQUENCY_HZ 1526u  // prescaler 3

/**
 * @brief PCA9685 configuration struct (wire format for CREATE).
 *
 * If the board has no OE pin control, set `.oe_pin = SYS_IO_PIN_NONE`.
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;      /**< I2C bus index */
  uint8_t i2c_addr;     /**< 7-bit I2C address (0x40 - 0x77) */
  sys_io_pin_t oe_pin;  /**< Optional active-low output enable pin */
} pca9685_cfg_t;

#ifdef __cplusplus
}
#endif
