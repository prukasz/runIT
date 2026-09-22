#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_dac53202/include/device_dac53202.h"

#define HEADER_packet_sys_device_install_dac53202_t 0x46
typedef struct __packed {
  uint8_t device_id; //@required @min 0 @max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;   //@required @min 0 @max 1
  uint8_t i2c_addr;  //@required @note not range-checked by driver_dac53202.c - no software-enforced bound
} packet_sys_device_install_dac53202_t;

static inline err_h decoder_packet_sys_device_install_dac53202_t(packet_sys_device_install_dac53202_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing dac53202 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_dac53202_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr};
  return d_dac53202_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_DAC53202_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_dac53202_t, packet_sys_device_install_dac53202_t, decoder_packet_sys_device_install_dac53202_t)
