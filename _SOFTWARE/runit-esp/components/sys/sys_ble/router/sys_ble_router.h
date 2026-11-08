#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_BLE (SYS_BLE.MD "Remote"), each above the frame it uses (packed, little-endian;
 * contract_ble.json names them), with its sides. Never renumber. Changes are staged; APPLY puts them live (with a
 * client connected, CONFIG_SYS_BLE_APPLY_DELAY_MS after its answer; the client reconnects). */

#define SYS_BLE_OP_SVC_CREATE 0x01u  // W: sys_ble_svc_create_args_t
typedef struct __attribute__((packed)) {
  uint16_t uuid;       /**< Service UUID */
  uint8_t is_primary;  /**< 1 = primary service, 0 = secondary */
} sys_ble_svc_create_args_t;

#define SYS_BLE_OP_SVC_REMOVE 0x02u  // W: sys_ble_svc_remove_args_t
typedef struct __attribute__((packed)) {
  uint16_t uuid;  /**< Service UUID */
} sys_ble_svc_remove_args_t;

#define SYS_BLE_OP_CHAR_CREATE 0x03u  // W: sys_ble_char_create_args_t + name (UTF-8, NUL-ended)
typedef struct __attribute__((packed)) {
  uint16_t svc_uuid;        /**< Parent service UUID */
  uint16_t uuid;            /**< Characteristic UUID */
  uint8_t is_write;         /**< 1 = writable, 0 = not writable */
  uint8_t is_indicate;      /**< 1 = indications enabled, 0 = disabled */
  uint8_t is_notify;        /**< 1 = notifications enabled, 0 = disabled */
  uint8_t is_framed;        /**< 1 = framed (fragment header on every packet), 0 = unframed */
  uint32_t tx_buffer_size;  /**< TX buffer size in bytes (0 = no TX) */
  uint32_t rx_buffer_size;  /**< RX buffer size in bytes (0 = no RX) */
  uint8_t has_provider;     /**< 1 = make it a provider (only an unframed one with a way in or out), 0 = none */
  uint8_t provider;         /**< Its provider's id, with has_provider */
} sys_ble_char_create_args_t;

#define SYS_BLE_OP_CHAR_REMOVE 0x04u  // W: sys_ble_char_remove_args_t
typedef struct __attribute__((packed)) {
  uint16_t svc_uuid;  /**< Parent service UUID */
  uint16_t uuid;      /**< Characteristic UUID */
} sys_ble_char_remove_args_t;

#define SYS_BLE_OP_APPLY 0x05u  // W: -

#define SYS_BLE_OP_STATUS 0x06u  // R: -> sys_ble_status_reply_t
typedef struct __attribute__((packed)) {
  uint8_t is_connected;        /**< 1 = connected, 0 = disconnected */
  uint16_t mtu;                /**< Negotiated MTU size in bytes */
  uint32_t rx_overflow_count;  /**< Count of dropped RX items due to full buffer */
} sys_ble_status_reply_t;

/* R: -> [count u8] then per service { u16 uuid, u8 flags (SYS_BLE_LIST_PRIMARY, SYS_BLE_LIST_LOCKED), u8 n,
 * n x { u16 uuid, u8 flags (SYS_BLE_LIST_WRITE, _INDICATE, _NOTIFY, _FRAMED, _PROVIDER), u32 tx, u32 rx, u8 provider (with SYS_BLE_LIST_PROVIDER),
 * name (NUL-ended) } } */
#define SYS_BLE_OP_LIST 0x07u

#define SYS_BLE_LIST_PRIMARY  (1u << 0)
#define SYS_BLE_LIST_LOCKED   (1u << 1)
#define SYS_BLE_LIST_WRITE    (1u << 0)
#define SYS_BLE_LIST_INDICATE (1u << 1)
#define SYS_BLE_LIST_NOTIFY   (1u << 2)
#define SYS_BLE_LIST_FRAMED   (1u << 3)
#define SYS_BLE_LIST_PROVIDER (1u << 4)

/**
 * @brief Runtime GATT table and BLE status, under ROUTER_KEY_BLE.
 * Ops and layouts: SYS_BLE.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_BLE, sys_ble_router));
 */
ROUTER_MODULE_DECLARE(sys_ble_router);

#ifdef __cplusplus
}
#endif
