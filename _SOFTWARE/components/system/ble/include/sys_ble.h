#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "sys_callbacks.h"
#include "sys_data_connector.h"
#include "sys_error.h"
#include "sys_error_ble.h"
#include <sdkconfig.h>
#include <stddef.h>
#include <stdint.h>

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

typedef enum sys_ble_events_e { SYS_BLE_EVENT_CONNECT = 0, SYS_BLE_EVENT_DISCONNECT, SYS_BLE_EVENT_FAILURE, SYS_BLE_EVENT_MAX } sys_ble_events_e;

/* Automatically deliver BLE events to the BLE route and the VM event buffer. */
#define SYS_BLE_CB(event_id, event_value) \
  do {                                                        \
    cb_event_t __cb_evt = {0};                                \
    __cb_evt.head.callback_type = CALLBACK_BLE;                \
    __cb_evt.head.route_mask = SYS_CB_ROUTE_BIT(SYS_CB_ROUTE_BLE) | SYS_CB_ROUTE_BIT(SYS_CB_ROUTE_VM); \
    __cb_evt.event.ble.event = (event_id);                    \
    __cb_evt.event.ble.value = (event_value);                 \
    SE_release(sys_callback_trigger(&__cb_evt));                          \
  } while (0)

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
err_h sys_ble_service_create(const sys_ble_svc_cfg_t* cfg);

/**
 * @brief Remove a BLE GATT service from the manager.
 *
 * If the driver is running and the service is registered with NimBLE, it will be
 * dynamically removed from the stack immediately.
 *
 * @param svc_uuid 16-bit UUID of the service to remove.
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_service_remove(uint16_t svc_uuid);

/**
 * @brief Create a new BLE GATT characteristic under a parent service.
 *
 * @param svc_uuid 16-bit UUID of the parent service.
 * @param cfg Pointer to characteristic configuration containing its UUID, properties, and optional TX/RX buffer sizes.
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_char_create(uint16_t svc_uuid, const sys_ble_char_cfg_t* cfg);

/**
 * @brief Remove a BLE GATT characteristic from a service.
 *
 * @param svc_uuid 16-bit UUID of the parent service.
 * @param char_uuid 16-bit UUID of the characteristic to remove.
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_char_remove(uint16_t svc_uuid, uint16_t char_uuid);

/**
 * @brief Probe whether a characteristic accepts RX.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @return err_h Status report (NULL if RX-enabled, ERR_BASE_INVALID_STATE if not).
 */
err_h sys_ble_char_check_rx_enabled(uint16_t char_uuid);

/**
 * @brief Dequeue incoming data written by a peer to a characteristic's RX buffer.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param buffer Target destination buffer to copy the dequeued data into.
 * @param max_len Maximum length of data to copy.
 * @param out_len Pointer to store the actual size of the dequeued data.
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_char_rx_dequeue(uint16_t char_uuid, uint8_t* buffer, size_t max_len, size_t* out_len);

/**
 * @brief Link a data connector to a characteristic for direct wake signaling on peer writes.
 *
 * When data arrives on @p char_uuid, @c conn->data_present is given directly.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param connector to be set when rx receiced and placed in buff, error returned when no more space or invalid char
 */
err_h sys_ble_char_link_connector(uint16_t char_uuid, sys_data_connector_t* connector);

/**
 * @brief Unlink a data connector from a characteristic.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param conn Pointer to data connector instance.
 * @return err_h Status report (NULL on success).
 */
err_h sys_ble_char_unlink_connector(uint16_t char_uuid, sys_data_connector_t* connector);

/**
 * @brief Test/debug utility: inject raw bytes into a characteristic's RX buffer
 * as if a peer had written them. Directly wakes any linked data connectors.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param data Pointer to the raw bytes to inject.
 * @param len Length of the raw bytes.
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_char_rx_inject(uint16_t char_uuid, const uint8_t* data, size_t len);

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
err_h sys_ble_char_send(uint16_t char_uuid, const uint8_t* data, size_t len, bool return_when_full);

/**
 * @brief Start the NimBLE host on first use, or synchronize runtime GATT changes.
 *
 * Must be called after creating or removing services and characteristics to apply changes.
 *
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_database_sync(void);

/**
 * @brief Get the current BLE connection state, MTU size, and RX overflow count.
 *
 * @param out_status Pointer to status structure to populate.
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_get_status(sys_ble_status_t* out_status);
