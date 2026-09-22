#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_tca6424a/include/device_tca6424a.h"

#define HEADER_packet_sys_device_install_tca6424a_t 0x42
typedef struct __packed {
  uint8_t device_id; //@required @min 0 @max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;   //@required @min 0 @max 1
  uint8_t i2c_addr;  //@required @note not range-checked by driver_tca6424a.c - no software-enforced bound
  uint8_t intr_pin_device_id; //@group interrupt-pin @role device_id
  uint8_t intr_pin_pin;       //@group interrupt-pin @role pin @sentinel SYS_GPIO_NONE
  uint8_t intr_pin_mode;      //@group interrupt-pin @role mode @ref sys_io_mode_e
  uint8_t rst_pin_device_id; //@group reset-pin @role device_id
  uint8_t rst_pin_pin;       //@group reset-pin @role pin @sentinel SYS_GPIO_NONE
  uint8_t rst_pin_mode;      //@group reset-pin @role mode @ref sys_io_mode_e
} packet_sys_device_install_tca6424a_t;

static inline err_h decoder_packet_sys_device_install_tca6424a_t(packet_sys_device_install_tca6424a_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing tca6424a (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_tca6424a_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                           .intr_pin = pin_ref_from_wire(packet->intr_pin_device_id, packet->intr_pin_pin, packet->intr_pin_mode),
                           .rst_pin = pin_ref_from_wire(packet->rst_pin_device_id, packet->rst_pin_pin, packet->rst_pin_mode)};
  return d_tca6424a_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_TCA6424A_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_tca6424a_t, packet_sys_device_install_tca6424a_t, decoder_packet_sys_device_install_tca6424a_t)
