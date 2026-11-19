#pragma once

#include "sys_device.h"
#include "sys_io.h"
#include "sys_power_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INA3221_CHANNELS     3u
#define INA3221_I2C_ADDR_MIN 0x40u  // the A0 strap (GND / Vs+ / SDA / SCL) selects 0x40..0x43
#define INA3221_I2C_ADDR_MAX 0x43u

/**
 * @brief INA3221 configuration struct (wire format for CREATE).
 *
 * If an alert pin is not wired, set it to SYS_IO_PIN_NONE.
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;        /**< I2C bus index */
  uint8_t i2c_addr;       /**< 7-bit I2C address (0x40 - 0x43) */
  sys_io_pin_t crit_pin;  /**< Critical alert active-low open-drain pin */
  sys_io_pin_t warn_pin;  /**< Warning alert active-low open-drain pin */
  uint8_t inverted_mask;  /**< Bitmask of channels with inverted shunt wiring (0..7) */
  uint16_t shunt_mohm;    /**< Resistance of each channel's shunt in mOhm (the current is the shunt voltage over it) */
} ina3221_cfg_t;

#ifdef __cplusplus
}
#endif
