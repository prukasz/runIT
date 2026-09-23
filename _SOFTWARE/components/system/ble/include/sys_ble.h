#pragma once
#include "utils.h"
#include "sys_event.h"
#include "sys_error.h"
#include "sys_error_ble.h"
#include <sdkconfig.h>

typedef struct {
  uint16_t uuid;  // 16-bit UUID
  bool is_primary;
} sys_ble_svc_cfg_t;

typedef struct {
  uint16_t uuid;  // 16-bit UUID
  bool is_write;
  bool is_indicate;
  bool is_notify;
  size_t tx_buffer_size; //if 0 then not used
  size_t rx_buffer_size; //if 0 then not used
  const char* desc; // Optional user description (GATT descriptor 0x2901)
} sys_ble_char_cfg_t;

//#ref-enum @alias BLE Event
typedef enum sys_ble_events_e {
  SYS_BLE_EVENT_CONNECT = 0,    //@alias Connected @description The app connected (value = connection handle)
  SYS_BLE_EVENT_DISCONNECT = 1, //@alias Disconnected @description The app disconnected (value = reason code)
  SYS_BLE_EVENT_FAILURE = 2,    //@alias Failure @description A connection, receive or send failure (value = error code)
} sys_ble_events_e;

/* Publish a BLE event (SYS_EVENT_DOMAIN_BLE). */
static inline SE_MUST_USE err_h sys_ble_publish(sys_ble_events_e event, int32_t value) {
  sys_event_t ev = {.domain = SYS_EVENT_DOMAIN_BLE, .event = (uint8_t)event, .value = value};
  return sys_event_publish(&ev);
}

typedef struct {
  bool is_connected;
  uint16_t mtu_size;
  uint32_t rx_overflow_count;
} sys_ble_status_t;

/* ========================================================================== *
 * Service and Characteristic API
 * ========================================================================== */

/**
 * @brief Reserved service UUID meaning "use the lazily-created default service".
 *
 * Passed as sys_ble_char_create()'s svc_uuid when the caller doesn't care about
 * GATT service grouping. The service is created on first use.
 */
#define SYS_BLE_SVC_DEFAULT_AUTO 0xFEFE

/**
 * @brief Create and register a new BLE GATT service config in the manager.
 *
 * @param cfg Pointer to service configuration struct containing the service UUID.
 * @return err_h Status report (NULL on success, or error status).
 */
SE_MUST_USE err_h sys_ble_service_create(const sys_ble_svc_cfg_t* cfg);

/**
 * @brief Remove a BLE GATT service from the manager.
 *
 * If the driver is running and the service is registered with NimBLE, it will be
 * dynamically removed from the stack immediately.
 *
 * @param svc_uuid 16-bit UUID of the service to remove.
 * @return err_h Status report (NULL on success, or error status).
 */
SE_MUST_USE err_h sys_ble_service_remove(uint16_t svc_uuid);

/**
 * @brief Create a new BLE GATT characteristic under a parent service.
 *
 * @param svc_uuid 16-bit UUID of the parent service.
 * @param cfg Pointer to characteristic configuration containing its UUID, properties, and optional TX/RX buffer sizes.
 * @return err_h Status report (NULL on success, or error status).
 */
SE_MUST_USE err_h sys_ble_char_create(uint16_t svc_uuid, const sys_ble_char_cfg_t* cfg);

/**
 * @brief Remove a BLE GATT characteristic from a service.
 *
 * @param svc_uuid 16-bit UUID of the parent service.
 * @param char_uuid 16-bit UUID of the characteristic to remove.
 * @return err_h Status report (NULL on success, or error status).
 */
SE_MUST_USE err_h sys_ble_char_remove(uint16_t svc_uuid, uint16_t char_uuid);

/**
 * @brief Probe whether a characteristic accepts RX.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @return err_h Status report (NULL if RX-enabled, ERR_BASE_INVALID_STATE if not).
 */
SE_MUST_USE err_h sys_ble_char_check_rx_enabled(uint16_t char_uuid);

/**
 * @brief Dequeue incoming data written by a peer to a characteristic's RX buffer.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param buffer Target destination buffer to copy the dequeued data into.
 * @param max_len Maximum length of data to copy.
 * @param out_len Pointer to store the actual size of the dequeued data.
 * @return err_h Status report (NULL on success, or error status).
 */
SE_MUST_USE err_h sys_ble_char_rx_dequeue(uint16_t char_uuid, uint8_t* buffer, size_t max_len, size_t* out_len);

/**
 * @brief Wake callback for a characteristic's RX consumer.
 *
 * Called from the BLE host task after a peer write was queued in the
 * characteristic's rx buffer. Must not block.
 */
typedef void (*sys_ble_rx_wake_f)(void* ctx);

/**
 * @brief Register a wake callback for peer writes on a characteristic.
 *
 * BLE knows nothing about its consumers: the data connector's BLE provider
 * registers a callback that wakes the connector's reader.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param wake Callback invoked after each queued write.
 * @param ctx Passed to @p wake; (wake, ctx) identifies the registration.
 * @return err_h NULL on success (also when already registered),
 *         ERR_BASE_INVALID_STATE if the characteristic has no rx buffer or is being removed,
 *         ERR_BASE_NO_MEM when CONFIG_SYS_BLE_MAX_LINKED_CONNECTORS slots are used.
 */
SE_MUST_USE err_h sys_ble_char_link_rx_wake(uint16_t char_uuid, sys_ble_rx_wake_f wake, void* ctx);

/**
 * @brief Remove a wake callback registered with sys_ble_char_link_rx_wake().
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param wake Registered callback.
 * @param ctx Registered context.
 * @return err_h NULL on success (also when it was not registered).
 */
SE_MUST_USE err_h sys_ble_char_unlink_rx_wake(uint16_t char_uuid, sys_ble_rx_wake_f wake, void* ctx);

/** @brief Containment for this module's CRITICAL errors (sys_errors domain hook, registered by the application). */
SE_MUST_USE err_h sys_ble_handle_fault(err_h node, err_h chain);

/**
 * @brief Test/debug utility: inject raw bytes into a characteristic's RX buffer
 * as if a peer had written them. Directly wakes any linked data connectors.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param data Pointer to the raw bytes to inject.
 * @param len Length of the raw bytes.
 * @return err_h Status report (NULL on success, or error status).
 */
SE_MUST_USE err_h sys_ble_char_rx_inject(uint16_t char_uuid, const uint8_t* data, size_t len);

/**
 * @brief Send data by enqueuing it into a characteristic's single TX ring buffer.
 *
 * The background BLE task will dequeue this data verbatim and transmit it as notification or indication.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param data Pointer to the data payload to send (already includes any connector framing).
 * @param len Length of the data payload.
 * @param return_when_full If true, returns immediately if the buffer is full (non-blocking).
 *                          If false, blocks for up to 100ms waiting for space.
 * @return err_h Status report (NULL on success, or error status).
 */
SE_MUST_USE err_h sys_ble_char_send(uint16_t char_uuid, const uint8_t* data, size_t len, bool return_when_full);

/**
 * @brief Start the NimBLE host on first use, or synchronize runtime GATT changes.
 *
 * Must be called after creating or removing services and characteristics to apply changes.
 *
 * @return err_h Status report (NULL on success, or error status).
 */
SE_MUST_USE err_h sys_ble_database_sync(void);

/**
 * @brief Largest item sys_ble_char_send() can deliver on a characteristic right now.
 *
 * The smaller of the link (negotiated ATT MTU - 3) and the largest item the
 * characteristic's TX buffer accepts. The link part starts at the BLE default
 * (23 - 3 = 20 bytes) on connect; the device requests the largest MTU itself,
 * so it rises as soon as the client answers. While disconnected nothing is
 * sent, so only the buffer limits it. 0 for an unknown characteristic or one
 * without a TX buffer.
 */
size_t sys_ble_char_max_payload(uint16_t char_uuid);

/**
 * @brief Get the current BLE connection state, MTU size, and RX overflow count.
 *
 * @param out_status Pointer to status structure to populate.
 * @return err_h Status report (NULL on success, or error status).
 */
SE_MUST_USE err_h sys_ble_get_status(sys_ble_status_t* out_status);
