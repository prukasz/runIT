#pragma once

#include "sys_device.h"
#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DAC53202_CHANNELS 2u
#define DAC53202_VREF_MV 3300  // both channels: VDD (3.3 V board supply) as reference, gain 1x

/**
 * @brief DAC53202 configuration struct (wire format for CREATE).
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;   /**< I2C bus index */
  uint8_t i2c_addr;  /**< 7-bit I2C address (0x48 - 0x4B) */
} dac53202_cfg_t;

#ifdef __cplusplus
}
#endif
