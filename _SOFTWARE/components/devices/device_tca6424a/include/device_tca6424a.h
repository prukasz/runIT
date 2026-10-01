#pragma once
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"

#define TCA6424A_TYPE_ID 0x42  /* the byte after 0x00 in a create frame: [0x00][0x42][d_tca6424a_cfg_t] */

// The annotations below describe the device to the app (data-structures/devices/*.generated.json,
// grammar: data-structures/auto-annotations/device/device-annotations.md).

//#device device_tca6424a
//  @title       TCA6424A GPIO expander
//  @description 24-bit I2C GPIO expander - adds extra digital input/output pins over I2C.
//  @protocol    i2c
//  @tags        i2c gpio io expander
//  @datasheet   https://www.ti.com/lit/ds/symlink/tca6424a.pdf
//  @type-id     TCA6424A_TYPE_ID
//  @contract-provider $SYS_DEVICE_CONTRACT_IO

//#self-property PIN
//  @one-of   [0..23]
//  @alias    Expander Pin

//#property PIN-MODE
//  @enum-ref sys_io_mode_e
//  @one-of   [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_OUTPUT_PUSH_PULL]

//#property INTR-MODE
//  @enum-ref sys_io_intr_mode_e
//  @one-of   [$SYS_IO_INTR_DISABLE, $SYS_IO_INTR_MODE_RISING_EDGE, $SYS_IO_INTR_MODE_FALLING_EDGE,
//             $SYS_IO_INTR_MODE_BOTH_EDGES]
//  @alias    Trigger Mode

//#contract packet_sys_io_reset_t
//  @alias       Reset pin
//  @description Releases the pin back to its unconfigured state.
//  @param pin   @arg PIN

//#contract packet_sys_io_set_mode_t
//  @alias       Configure pin mode
//  @description This expander only supports plain digital input or output - no pull resistors,
//               open-drain, ADC, or PWM.
//  @param pin   @arg PIN
//  @param mode  @arg PIN-MODE

//#contract packet_sys_io_configure_intr_t
//  @alias       Configure pin interrupt
//  @description Digital edge-triggered interrupt only - this expander has no ADC, so the
//               window-comparator trigger modes don't apply.
//  @param pin   @arg PIN
//  @param mode  @arg INTR-MODE

//#contract packet_sys_io_set_level_t
//  @alias       Set pin level
//  @param pin   @arg PIN
//  @param level @type bool

//#contract packet_sys_io_get_level_t
//  @alias       Read pin level
//  @param pin   @arg PIN
//  @returns     level

//#contract packet_sys_io_toggle_t
//  @alias       Toggle pin
//  @description Flip an output pin's current level.
//  @param pin   @arg PIN

/**
 * @brief TCA6424A configuration. Also the wire struct of the create frame (packed, device_id first).
 *
 * @warning A pin that is not wired MUST be spelled `SYS_IO_PIN_NONE_INIT`; omitting the field
 *          zero-fills it to device 0 / pin 0, which is a real pin.
 */
typedef struct __packed {
  uint8_t device_id;       //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;        //@alias I2C Address @one-of [0x22, 0x23] @note ADDR low selects 0x22; high selects 0x23.
  sys_io_pin_ref_t intr_pin; //@modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP]
  sys_io_pin_ref_t rst_pin;
} d_tca6424a_cfg_t;

/** The TCA6424A device class: register with sys_device_register_class(), create with SYS_DEVICE_CREATE(&g_tca6424a_class, &cfg). */
extern const sys_device_class_t g_tca6424a_class;
