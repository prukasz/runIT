#pragma once
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"

#define TPS55289_TYPE_ID 0x43  /* the byte after 0x00 in a create frame: [0x00][0x43][d_tps55289_cfg_t] */

#define DEVICE_TPS55289_MAX_VOLTAGE_MV 20000
#define DEVICE_TPS55289_MIN_VOLTAGE_MV 3000

#define DEVICE_TPS55289_MAX_CURRENT_MA 5500
#define DEVICE_TPS55289_MIN_CURRENT_MA 200

// The annotations below describe the device to the app (data-structures/devices/*.generated.json,
// grammar: data-structures/auto-annotations/device/device-annotations.md).

//#device device_tps55289
//  @title       TPS55289 buck-boost regulator
//  @description Adjustable I2C buck-boost voltage regulator - a programmable power output with a current
//               limit and enable control.
//  @protocol    i2c
//  @tags        i2c power voltage current regulator
//  @datasheet   https://www.ti.com/lit/ds/symlink/tps55289.pdf
//  @type-id     TPS55289_TYPE_ID
//  @contract-provider $SYS_DEVICE_CONTRACT_POWER_VREG

//#contract packet_sys_power_vreg_set_enable_t
//  @alias       Enable output
//  @param state @alias Enabled @type bool

//#contract packet_sys_power_vreg_set_voltage_t
//  @alias       Set output voltage
//  @param voltage_mV

//#contract packet_sys_power_vreg_set_current_t
//  @alias       Set output current limit
//  @param current_mA

/**
 * @brief TPS55289 configuration. Also the wire struct of the create frame (packed, device_id first).
 *
 * @warning A pin that is not wired MUST be spelled `SYS_IO_PIN_NONE_INIT`; omitting the field
 *          zero-fills it to device 0 / pin 0, which is a real pin.
 */
typedef struct __packed {
  uint8_t device_id;       //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;        //@alias I2C Address @one-of [0x74, 0x75] @note The MODE pin selects one of two I2C addresses.
  sys_io_pin_ref_t intr_pin; //@modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP]
  sys_io_pin_ref_t en_pin;
} d_tps55289_cfg_t;

/** The TPS55289 device class: register with sys_device_register_class(), create with SYS_DEVICE_CREATE(&g_tps55289_class, &cfg). */
extern const sys_device_class_t g_tps55289_class;
