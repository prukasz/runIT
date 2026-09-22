#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_ap33772s/include/device_ap33772s.h"

#define HEADER_packet_sys_device_install_ap33772s_t 0x45
typedef struct __packed {
  uint8_t device_id; //@required @min 0 @max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;   //@required @min 0 @max 1
  uint8_t i2c_addr;  //@required @default 0x52 @note AP33772S has a fixed datasheet address (AP33772S_ADDRESS); the field is honored (adapter_ap33772s.c overwrites the driver handle's address with it) but the real chip only answers at 0x52
  uint8_t intr_pin_device_id; //@group interrupt-pin @role device_id
  uint8_t intr_pin_pin;       //@group interrupt-pin @role pin @sentinel SYS_GPIO_NONE
  uint8_t intr_pin_mode;      //@group interrupt-pin @role mode @ref sys_io_mode_e
} packet_sys_device_install_ap33772s_t;

static inline err_h decoder_packet_sys_device_install_ap33772s_t(packet_sys_device_install_ap33772s_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing ap33772s (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_ap33772s_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                           .intr_pin = pin_ref_from_wire(packet->intr_pin_device_id, packet->intr_pin_pin, packet->intr_pin_mode)};
  return d_ap33772s_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_AP33772S_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_ap33772s_t, packet_sys_device_install_ap33772s_t, decoder_packet_sys_device_install_ap33772s_t)
