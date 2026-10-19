#pragma once

#include "sys_device.h"
#include "sys_io.h"
#include "sys_power_monitor.h"
#include "sys_power_usb_pd.h"
#include "sys_power_vreg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AP33772S_I2C_ADDR 0x52u  // fixed 7-bit I2C address

/**
 * @brief AP33772S configuration struct (wire format for CREATE).
 *
 * If the interrupt pin is not wired, set it to SYS_IO_PIN_NONE.
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;        /**< I2C bus index */
  uint8_t i2c_addr;       /**< 7-bit I2C address (0x52) */
  sys_io_pin_t intr_pin;  /**< Active-low interrupt pin (optional) */
} ap33772s_cfg_t;

#ifdef __cplusplus
}
#endif
