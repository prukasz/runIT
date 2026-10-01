#pragma once
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"

#define DAC53202_TYPE_ID 0x46  /* the byte after 0x00 in a create frame: [0x00][0x46][d_dac53202_cfg_t] */

// The annotations below describe the device to the app (data-structures/devices/*.generated.json,
// grammar: data-structures/auto-annotations/device/device-annotations.md).

//#device device_dac53202
//  @title       DAC53202 dual DAC
//  @description Two-channel I2C digital-to-analog converter - outputs a steady voltage on each channel.
//  @protocol    i2c
//  @tags        i2c dac voltage
//  @datasheet   https://www.ti.com/lit/ds/symlink/dac53202.pdf
//  @type-id     DAC53202_TYPE_ID
//  @contract-provider $SYS_DEVICE_CONTRACT_IO

//#self-property CHANNEL
//  @one-of   [0, 1]
//  @alias    DAC Channel

//#contract packet_sys_io_reset_t
//  @alias       Reset channel
//  @description Power off the selected DAC channel.
//  @param pin   @arg CHANNEL

//#contract packet_sys_io_get_voltage_t
//  @alias       Read channel voltage
//  @description Reads back the last voltage this channel was set to.
//  @param pin   @arg CHANNEL
//  @returns     voltage_mV

//#contract packet_sys_io_set_voltage_t
//  @alias       Set channel voltage
//  @description Drive the selected channel to an exact output voltage.
//  @param pin   @arg CHANNEL
//  @param voltage_mV

/**
 * @brief DAC53202 configuration. Also the wire struct of the create frame (packed, device_id first).
 */
typedef struct __packed {
  uint8_t device_id; //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;  //@alias I2C Address @one-of [0x48..0x4B] @note The A0 pin selects one of four I2C addresses.
} d_dac53202_cfg_t;

/** The DAC53202 device class: register with sys_device_register_class(), create with SYS_DEVICE_CREATE(&g_dac53202_class, &cfg). */
extern const sys_device_class_t g_dac53202_class;
