#pragma once
#include "sys_error.h"
#include "sys_io.h"

/**
 * @brief PCA9685 PWM expander configuration.
 *
 * @warning A board without OE control MUST spell it `.oe_pin = SYS_IO_PIN_NONE`.
 *          Omitting the field zero-fills it to device 0 / pin 0, which is a real
 *          pin on a real device - not "unused".
 */
typedef struct d_pca9685_cfg_t {
  uint8_t device_id;      /* MUST be first - SYS_DEVICE_CREATE reads it */
  bool i2c_bus;           /* I2C bus number (0 or 1) */
  uint8_t i2c_addr;       /* 7-bit I2C address of the PCA9685 */
  sys_io_pin_ref_t oe_pin; /* Output-Enable pin, or SYS_IO_PIN_NONE */
} d_pca9685_cfg_t;

/**
 * @brief Initialize and register the PCA9685 PWM Expander device.
 *
 * @param cfg Device configuration; copied, so it may be a compound literal.
 * @return err_h Status of the registration
 */
err_h d_pca9685_create(const d_pca9685_cfg_t* cfg);