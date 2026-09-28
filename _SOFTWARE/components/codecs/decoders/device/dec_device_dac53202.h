#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_dac53202/include/device_dac53202.h"

//#device device_dac53202
//  @title       DAC53202 dual DAC
//  @description Two-channel I2C digital-to-analog converter - outputs a steady voltage on each channel.
//  @protocol    i2c
//  @tags        i2c dac voltage
//  @datasheet   https://www.ti.com/lit/ds/symlink/dac53202.pdf
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

#define HEADER_packet_sys_device_install_dac53202_t 0x46
typedef struct __packed {
  uint8_t device_id; //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;  //@alias I2C Address @one-of [0x48..0x4B] @note The A0 pin selects one of four I2C addresses.
} packet_sys_device_install_dac53202_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_install_dac53202_t(packet_sys_device_install_dac53202_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing dac53202 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_dac53202_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr};
  return d_dac53202_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_DAC53202_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_dac53202_t, packet_sys_device_install_dac53202_t, decoder_packet_sys_device_install_dac53202_t)
