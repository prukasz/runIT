#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_gpio_esp/include/device_gpio_esp.h"

#define HEADER_packet_sys_device_install_gpio_esp_t 0x40
typedef struct __packed {
  uint8_t device_id; //@required @min 0 @max CONFIG_SYS_DEVICE_MAX_ID
} packet_sys_device_install_gpio_esp_t;

static inline err_h decoder_packet_sys_device_install_gpio_esp_t(packet_sys_device_install_gpio_esp_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing gpio_esp (dev %u)", packet->device_id);
  d_gpio_esp_cfg_t cfg = {.device_id = packet->device_id};
  return d_gpio_esp_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_GPIO_ESP_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_gpio_esp_t, packet_sys_device_install_gpio_esp_t, decoder_packet_sys_device_install_gpio_esp_t)
