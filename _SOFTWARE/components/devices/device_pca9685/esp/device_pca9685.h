#pragma once
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"

#define PCA9685_TYPE_ID 0x41  /* the byte after 0x00 in a create frame: [0x00][0x41][d_pca9685_cfg_t] */

// The device's description for the app (title, notes, contracts, limits) is device_pca9685.json
// next to the .c; its limits become include/device_pca9685_limits.generated.h.

/**
 * @brief PCA9685 PWM expander configuration. This is also the wire struct of the
 * create frame (packed, device_id first): the router hands the received bytes to
 * sys_device_create(&g_pca9685_class, ...) as they are, and the generator reads the fields from here.
 *
 * @warning A board without OE control MUST spell it `.oe_pin = SYS_IO_PIN_NONE_INIT`.
 *          Omitting the field zero-fills it to device 0 / pin 0, which is a real
 *          pin on a real device - not "unused".
 */
typedef struct __packed {
  uint8_t device_id;
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  sys_io_pin_ref_t oe_pin;
} d_pca9685_cfg_t;

/**
 * @brief The PCA9685 device class: register it with sys_device_register_class(), create a
 * device with SYS_DEVICE_CREATE(&g_pca9685_class, &(d_pca9685_cfg_t){...}). The cfg is only
 * read during the call, so it may be a compound literal; ERR_DEV_PIN_ORDER if the OE pin
 * isn't on a lower-ID device.
 */
extern const sys_device_class_t g_pca9685_class;
