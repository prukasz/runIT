#pragma once

#include "sys_device.h"
#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADS7128_CHANNELS 8u

/**
 * @brief ADS7128 configuration struct (wire format for CREATE).
 *
 * If ALERT pin is not wired, set `.intr_pin = SYS_IO_PIN_NONE`.
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;          /**< I2C bus index */
  uint8_t i2c_addr;         /**< 7-bit I2C address */
  sys_io_pin_t intr_pin;    /**< Active-low ALERT open-drain interrupt pin */
  uint16_t full_scale_mV;   /**< Full scale input voltage in mV (e.g. 20000 mV) */
} ads7128_cfg_t;

#ifdef __cplusplus
}
#endif
