#pragma once
/**
 * @file dec_settings_ble.h
 * @brief Runtime BLE GATT configuration packets.
 *
 * Wire format: [class] [packet] [payload].  Multi-byte values are little-endian.
 * The characteristic-create payload ends with a NUL-terminated name; it becomes
 * the characteristic's 0x2901 user-description descriptor.
 */

#include <stddef.h>
#include <string.h>
#include <sdkconfig.h>
#include "sys_ble.h"
#include "sys_error.h"
#include "sys_interface.h"

//@settings ble @title BLE settings @description Create and remove runtime BLE GATT services and characteristics.
//@settings-class SETTINGS_BLE

#undef OWNER
#define OWNER OWNER_DEC_SETTINGS_BLE

#define DEC_SETTINGS_BLE_TAG "dec_settings_ble"

#define HEADER_packet_settings_ble_service_create_t 0x01
typedef struct __packed {
  uint16_t uuid;       //@required @alias Service UUID
  uint8_t is_primary;  //@required @alias Primary Service
} packet_settings_ble_service_create_t;

#define HEADER_packet_settings_ble_service_remove_t 0x02
typedef struct __packed {
  uint16_t uuid; //@required @alias Service UUID
} packet_settings_ble_service_remove_t;

#define HEADER_packet_settings_ble_char_create_t 0x03
typedef struct __packed {
  uint16_t service_uuid;   //@required @alias Service UUID
  uint16_t uuid;           //@required @alias Characteristic UUID
  uint8_t is_write;        //@required @alias Writable
  uint8_t is_indicate;     //@required @alias Indicate
  uint8_t is_notify;       //@required @alias Notify
  uint32_t tx_buffer_size; //@required @alias TX Buffer Size @unit bytes
  uint32_t rx_buffer_size; //@required @alias RX Buffer Size @unit bytes
  char name[];             //@required @alias Characteristic Name @encoding utf-8 @terminator nul
} packet_settings_ble_char_create_t;

#define HEADER_packet_settings_ble_char_remove_t 0x04
typedef struct __packed {
  uint16_t service_uuid; //@required @alias Service UUID
  uint16_t uuid;         //@required @alias Characteristic UUID
} packet_settings_ble_char_remove_t;

static inline SE_MUST_USE err_h dec_settings_ble_sync(void) {
  return sys_ble_database_sync();
}

static inline SE_MUST_USE err_h dec_settings_ble_service_create(const uint8_t* body, size_t len) {
  if (len < sizeof(packet_settings_ble_service_create_t)) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = sizeof(packet_settings_ble_service_create_t));
  }
  const packet_settings_ble_service_create_t* packet = (const void*)body;
  const sys_ble_svc_cfg_t cfg = {.uuid = packet->uuid, .is_primary = packet->is_primary != 0};
  SE_TRY(sys_ble_service_create(&cfg));
  return dec_settings_ble_sync();
}

static inline SE_MUST_USE err_h dec_settings_ble_service_remove(const uint8_t* body, size_t len) {
  if (len < sizeof(packet_settings_ble_service_remove_t)) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = sizeof(packet_settings_ble_service_remove_t));
  }
  const packet_settings_ble_service_remove_t* packet = (const void*)body;
  SE_TRY(sys_ble_service_remove(packet->uuid));
  return dec_settings_ble_sync();
}

static inline SE_MUST_USE err_h dec_settings_ble_char_create(const uint8_t* body, size_t len) {
  if (len < offsetof(packet_settings_ble_char_create_t, name) + 1u) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = (uint32_t)(offsetof(packet_settings_ble_char_create_t, name) + 1u));
  }
  const packet_settings_ble_char_create_t* packet = (const void*)body;
  const size_t name_len = len - offsetof(packet_settings_ble_char_create_t, name);
  if (!memchr(packet->name, '\0', name_len)) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = (uint32_t)(len + 1u));
  }
  const sys_ble_char_cfg_t cfg = {
      .uuid = packet->uuid,
      .is_write = packet->is_write != 0,
      .is_indicate = packet->is_indicate != 0,
      .is_notify = packet->is_notify != 0,
      .tx_buffer_size = packet->tx_buffer_size,
      .rx_buffer_size = packet->rx_buffer_size,
      .desc = packet->name,
  };
  SE_TRY(sys_ble_char_create(packet->service_uuid, &cfg));
  return dec_settings_ble_sync();
}

static inline SE_MUST_USE err_h dec_settings_ble_char_remove(const uint8_t* body, size_t len) {
  if (len < sizeof(packet_settings_ble_char_remove_t)) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = sizeof(packet_settings_ble_char_remove_t));
  }
  const packet_settings_ble_char_remove_t* packet = (const void*)body;
  SE_TRY(sys_ble_char_remove(packet->service_uuid, packet->uuid));
  return dec_settings_ble_sync();
}

static inline SE_MUST_USE err_h dec_settings_ble_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }

  switch (data[0]) {
    case HEADER_packet_settings_ble_service_create_t:
      return dec_settings_ble_service_create(data + 1, len - 1);
    case HEADER_packet_settings_ble_service_remove_t:
      return dec_settings_ble_service_remove(data + 1, len - 1);
    case HEADER_packet_settings_ble_char_create_t:
      return dec_settings_ble_char_create(data + 1, len - 1);
    case HEADER_packet_settings_ble_char_remove_t:
      return dec_settings_ble_char_remove(data + 1, len - 1);
    default:
      SE_FAIL(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = CONFIG_RX_PACKET_CLASS_SETTINGS_BLE, .packet_header = data[0]);
  }
}
