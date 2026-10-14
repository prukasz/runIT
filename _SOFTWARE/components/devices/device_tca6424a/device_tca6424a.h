#pragma once

#include "sys_device.h"
#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TCA6424A_PINS 24u
#define TCA6424A_I2C_ADDR_LOW 0x22u   // ADDR low
#define TCA6424A_I2C_ADDR_HIGH 0x23u  // ADDR high

/**
 * @brief TCA6424A configuration struct (wire format for CREATE).
 *
 * If a pin is not wired, set it to SYS_IO_PIN_NONE.
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;        /**< I2C bus index */
  uint8_t i2c_addr;       /**< 7-bit I2C address (0x22 or 0x23) */
  sys_io_pin_t intr_pin;  /**< Active-low open-drain interrupt pin */
  sys_io_pin_t rst_pin;   /**< Active-low reset pin */
} tca6424a_cfg_t;

#ifdef __cplusplus
}
#endif
