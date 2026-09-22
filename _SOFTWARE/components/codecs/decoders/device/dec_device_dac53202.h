#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_dac53202/include/device_dac53202.h"

//@id device_dac53202
//@version 1.0.0
//@title DAC53202 dual DAC
//@description Two-channel I2C digital-to-analog converter - outputs a steady voltage on each channel.
//@protocol i2c
//@tags i2c dac voltage
//@contract-provider $SYS_DEVICE_CONTRACT_IO
//@self-property CHANNEL @one-of [0,1]

//@contract packet_sys_io_reset_t @alias Reset channel
//@param pin @arg CHANNEL @alias DAC Channel
//@description Power off the selected DAC channel.

//@contract packet_sys_io_get_voltage_t @alias Read channel voltage
//@param pin @arg CHANNEL @alias DAC Channel
//@returns voltage_mV @type uint32_t @unit mV
//@description Reads back the last voltage this channel was set to.

//@contract packet_sys_io_set_voltage_t @alias Set channel voltage
//@param pin @arg CHANNEL @alias DAC Channel
//@param voltage_mV @alias Voltage @type uint32_t @unit mV
//@description Drive the selected channel to an exact output voltage.

#define HEADER_packet_sys_device_install_dac53202_t 0x46
typedef struct __packed {
  uint8_t device_id; //@required @min 0 @max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;   //@required @min 0 @max 1
  uint8_t i2c_addr;  //@required @note not range-checked by driver_dac53202.c - no software-enforced bound
} packet_sys_device_install_dac53202_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_install_dac53202_t(packet_sys_device_install_dac53202_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing dac53202 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_dac53202_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr};
  return d_dac53202_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_DAC53202_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_dac53202_t, packet_sys_device_install_dac53202_t, decoder_packet_sys_device_install_dac53202_t)
