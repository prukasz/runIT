#pragma once
/**
 * @file dec_settings_data_connector.h
 * @brief Runtime data-connector topology packets.
 *
 * Wire format: [class] [packet] [payload]. Multi-byte values are little-endian.
 * Provider parameters are opaque 32-bit values; the BLE provider uses the low
 * 16 bits as its characteristic UUID.
 */

#include <stddef.h>
#include <string.h>
#include <sdkconfig.h>
#include "sys_data_connector.h"
#include "sys_error.h"
#include "sys_interface.h"

//@settings data-connector @title Data connector settings @description Create connectors and configure their runtime transport bindings.
//@settings-class SETTINGS_DATA_CONNECTOR

#undef OWNER
#define OWNER OWNER_DEC_SETTINGS_DATA_CONNECTOR

#define HEADER_packet_settings_data_connector_create_t 0x01
typedef struct __packed {
  uint8_t id;              //@required @alias Connector ID
  uint8_t header;          //@required @alias Frame Header
  uint16_t max_packet_len; //@required @alias Maximum Packet Length @unit bytes
  char name[];             //@required @alias Connector Name @encoding utf-8 @terminator nul
} packet_settings_data_connector_create_t;

#define HEADER_packet_settings_data_connector_remove_t 0x02
typedef struct __packed {
  uint8_t id; //@required @alias Connector ID
} packet_settings_data_connector_remove_t;

#define HEADER_packet_settings_data_connector_rx_add_t 0x03
typedef struct __packed {
  uint8_t connector_id;   //@required @alias Connector ID
  uint8_t provider_id;    //@required @alias Provider ID
  uint32_t provider_param; //@required @alias Provider Parameter
} packet_settings_data_connector_rx_add_t;

#define HEADER_packet_settings_data_connector_rx_remove_t 0x04
typedef struct __packed {
  uint8_t connector_id; //@required @alias Connector ID
  uint8_t provider_id;  //@required @alias Provider ID
} packet_settings_data_connector_rx_remove_t;

#define HEADER_packet_settings_data_connector_tx_add_t 0x05
typedef struct __packed {
  uint8_t connector_id;    //@required @alias Connector ID
  uint8_t provider_id;     //@required @alias Provider ID
  uint32_t provider_param; //@required @alias Provider Parameter
} packet_settings_data_connector_tx_add_t;

#define HEADER_packet_settings_data_connector_tx_remove_t 0x06
typedef struct __packed {
  uint8_t connector_id; //@required @alias Connector ID
  uint8_t provider_id;  //@required @alias Provider ID
} packet_settings_data_connector_tx_remove_t;

#define HEADER_packet_settings_data_connector_suspend_t 0x07
typedef struct __packed {
  uint8_t id; //@required @alias Connector ID
} packet_settings_data_connector_suspend_t;

#define HEADER_packet_settings_data_connector_resume_t 0x08
typedef struct __packed {
  uint8_t id; //@required @alias Connector ID
} packet_settings_data_connector_resume_t;

static inline SE_MUST_USE err_h dec_settings_data_connector_create(const uint8_t* body, size_t len) {
  if (len < offsetof(packet_settings_data_connector_create_t, name) + 1u) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = (uint32_t)(offsetof(packet_settings_data_connector_create_t, name) + 1u));
  }
  const packet_settings_data_connector_create_t* packet = (const void*)body;
  const size_t name_len = len - offsetof(packet_settings_data_connector_create_t, name);
  if (!memchr(packet->name, '\0', name_len)) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = (uint32_t)(len + 1u));
  }
  const sys_data_connector_cfg_t cfg = {
      .id = packet->id,
      .name = packet->name,
      .header = packet->header,
      .max_packet_len = packet->max_packet_len,
  };
  if (!sys_data_connector_create_with_cfg(&cfg)) {
    SE_FAIL(ERR_BASE_NO_MEM, packet->id);
  }
  return NULL;
}

static inline SE_MUST_USE err_h dec_settings_data_connector_remove(const uint8_t* body, size_t len) {
  if (len < sizeof(packet_settings_data_connector_remove_t)) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = sizeof(packet_settings_data_connector_remove_t));
  }
  return sys_data_connector_remove(((const packet_settings_data_connector_remove_t*)(const void*)body)->id);
}

static inline SE_MUST_USE err_h dec_settings_data_connector_rx_add(const uint8_t* body, size_t len) {
  if (len < sizeof(packet_settings_data_connector_rx_add_t)) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = sizeof(packet_settings_data_connector_rx_add_t));
  }
  const packet_settings_data_connector_rx_add_t* packet = (const void*)body;
  sys_data_connector_t* conn = sys_data_connector_get(packet->connector_id);
  if (!conn) SE_FAIL(ERR_BASE_NOT_FOUND, packet->connector_id);
  return sys_data_connector_bind_rx(conn, packet->provider_id, (void*)(uintptr_t)packet->provider_param);
}

static inline SE_MUST_USE err_h dec_settings_data_connector_rx_remove(const uint8_t* body, size_t len) {
  if (len < sizeof(packet_settings_data_connector_rx_remove_t)) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = sizeof(packet_settings_data_connector_rx_remove_t));
  }
  const packet_settings_data_connector_rx_remove_t* packet = (const void*)body;
  sys_data_connector_t* conn = sys_data_connector_get(packet->connector_id);
  if (!conn) SE_FAIL(ERR_BASE_NOT_FOUND, packet->connector_id);
  return sys_data_connector_unbind_rx(conn, packet->provider_id);
}

static inline SE_MUST_USE err_h dec_settings_data_connector_tx_add(const uint8_t* body, size_t len) {
  if (len < sizeof(packet_settings_data_connector_tx_add_t)) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = sizeof(packet_settings_data_connector_tx_add_t));
  }
  const packet_settings_data_connector_tx_add_t* packet = (const void*)body;
  sys_data_connector_t* conn = sys_data_connector_get(packet->connector_id);
  if (!conn) SE_FAIL(ERR_BASE_NOT_FOUND, packet->connector_id);
  return sys_data_connector_bind_tx(conn, packet->provider_id, (void*)(uintptr_t)packet->provider_param);
}

static inline SE_MUST_USE err_h dec_settings_data_connector_tx_remove(const uint8_t* body, size_t len) {
  if (len < sizeof(packet_settings_data_connector_tx_remove_t)) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = sizeof(packet_settings_data_connector_tx_remove_t));
  }
  const packet_settings_data_connector_tx_remove_t* packet = (const void*)body;
  sys_data_connector_t* conn = sys_data_connector_get(packet->connector_id);
  if (!conn) SE_FAIL(ERR_BASE_NOT_FOUND, packet->connector_id);
  return sys_data_connector_unbind_tx(conn, packet->provider_id);
}

static inline SE_MUST_USE err_h dec_settings_data_connector_suspend(const uint8_t* body, size_t len) {
  if (len < sizeof(packet_settings_data_connector_suspend_t)) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = sizeof(packet_settings_data_connector_suspend_t));
  }
  const packet_settings_data_connector_suspend_t* packet = (const void*)body;
  sys_data_connector_t* conn = sys_data_connector_get(packet->id);
  if (!conn) SE_FAIL(ERR_BASE_NOT_FOUND, packet->id);
  sys_data_connector_suspend(conn);
  return NULL;
}

static inline SE_MUST_USE err_h dec_settings_data_connector_resume(const uint8_t* body, size_t len) {
  if (len < sizeof(packet_settings_data_connector_resume_t)) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = sizeof(packet_settings_data_connector_resume_t));
  }
  const packet_settings_data_connector_resume_t* packet = (const void*)body;
  sys_data_connector_t* conn = sys_data_connector_get(packet->id);
  if (!conn) SE_FAIL(ERR_BASE_NOT_FOUND, packet->id);
  sys_data_connector_resume(conn);
  return NULL;
}

static inline SE_MUST_USE err_h dec_settings_data_connector_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }

  switch (data[0]) {
    case HEADER_packet_settings_data_connector_create_t:
      return dec_settings_data_connector_create(data + 1, len - 1);
    case HEADER_packet_settings_data_connector_remove_t:
      return dec_settings_data_connector_remove(data + 1, len - 1);
    case HEADER_packet_settings_data_connector_rx_add_t:
      return dec_settings_data_connector_rx_add(data + 1, len - 1);
    case HEADER_packet_settings_data_connector_rx_remove_t:
      return dec_settings_data_connector_rx_remove(data + 1, len - 1);
    case HEADER_packet_settings_data_connector_tx_add_t:
      return dec_settings_data_connector_tx_add(data + 1, len - 1);
    case HEADER_packet_settings_data_connector_tx_remove_t:
      return dec_settings_data_connector_tx_remove(data + 1, len - 1);
    case HEADER_packet_settings_data_connector_suspend_t:
      return dec_settings_data_connector_suspend(data + 1, len - 1);
    case HEADER_packet_settings_data_connector_resume_t:
      return dec_settings_data_connector_resume(data + 1, len - 1);
    default:
      SE_FAIL(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = CONFIG_RX_PACKET_CLASS_SETTINGS_DATA_CONNECTOR, .packet_header = data[0]);
  }
}
