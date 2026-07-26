#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "sys_error.h"
#include "sys_error_ble.h"
#include "sys_callbacks.h"

#define MAX_TX_BUFFERS 3

typedef struct {
  uint16_t uuid;  // 16-bit UUID
  bool is_primary;
} sys_ble_svc_cfg_t;

typedef struct {
  uint16_t uuid;  // 16-bit UUID
  bool is_read;
  bool is_write;
  bool is_indicate;
  bool is_notify;
  const char* desc;  // Optional user description (GATT descriptor 0x2901)
} sys_ble_char_cfg_t;

typedef struct {
  sys_ble_char_cfg_t info;
  size_t rx_buffer_size;  // Size of RX ring buffer (0 if write/notify is disabled)
} sys_ble_char_create_t;

typedef struct {
  uint8_t header;   // stream tag; also identifies this TX slot within its characteristic
  size_t size;      // ring buffer capacity in bytes
  bool is_indication;
} sys_ble_tx_buf_cfg_t;

typedef enum sys_ble_events_e { SYS_BLE_EVENT_CONNECT = 0, SYS_BLE_EVENT_DISCONNECT, SYS_BLE_EVENT_FAILURE, SYS_BLE_EVENT_MAX } sys_ble_events_e;

#define SYS_BLE_CB(event_id, event_value, mask) \
  do {                                          \
    cb_event_t __cb_evt = {0};                  \
    __cb_evt.head.callback_type = CALLBACK_BLE; \
    __cb_evt.head.route_mask = (mask); \
    __cb_evt.event.ble.event = (event_id);      \
    __cb_evt.event.ble.value = (event_value);   \
    sys_callback_trigger(&__cb_evt);            \
  } while (0)

typedef struct {
  bool is_connected;
  uint16_t mtu_size;
  uint32_t rx_overflow_count;
} sys_ble_status_t;

/* ========================================================================== *
 * Unified Declarative Channel API
 * ========================================================================== */

/**
 * @brief Reserved service UUID meaning "use the lazily-created default service".
 *
 * Passed as sys_ble_channel_cfg_t.svc_uuid when the caller doesn't care about
 * GATT service grouping. The service is created on first use.
 */
#define SYS_BLE_SVC_DEFAULT_AUTO 0xFEFE

typedef enum {
  SYS_BLE_RX_MODE_NONE = 0,     // RX disabled (rx_buffer_size ignored)
  SYS_BLE_RX_MODE_POLL,         // App drains via sys_ble_char_rx_dequeue()/sys_ble_char_get_rx_semaphore()
  SYS_BLE_RX_MODE_CALLBACK,     // rx_handler is dispatched (via the callbacks system) on each incoming write
} sys_ble_rx_mode_e;

/**
 * @brief Declarative description of one BLE "data channel": a characteristic
 * plus its RX/TX plumbing, collapsing what otherwise takes several manual
 * sys_ble_service_create() / sys_ble_char_create() / sys_ble_char_assign_tx_buffer()
 * calls into a single sys_ble_channel_create() call.
 */
typedef struct {
  uint16_t svc_uuid;  // 0 or SYS_BLE_SVC_DEFAULT_AUTO to use the lazily-created default service
  sys_ble_char_cfg_t chr;
  size_t rx_buffer_size;  // 0 if rx_mode == SYS_BLE_RX_MODE_NONE
  sys_ble_rx_mode_e rx_mode;
  own_func_t rx_handler;  // used only when rx_mode == SYS_BLE_RX_MODE_CALLBACK

  const sys_ble_tx_buf_cfg_t* tx_bufs;  // optional array, NULL/0 count if the channel is RX-only
  uint8_t tx_buf_count;
} sys_ble_channel_cfg_t;

/**
 * @brief Create a BLE data channel (service-if-needed + characteristic + TX
 * buffers + RX delivery mode) from one declarative config.
 *
 * Reuses an existing service if cfg->svc_uuid already exists (unlike the raw
 * sys_ble_service_create(), which errors on a duplicate), or lazily creates
 * the default service when cfg->svc_uuid is 0 or SYS_BLE_SVC_DEFAULT_AUTO.
 *
 * @param cfg Channel configuration.
 * @param sync_now If true, calls sys_ble_database_sync() before returning -
 *                  use for a single dynamic post-boot addition. If false,
 *                  the caller must call sys_ble_database_sync() once after
 *                  batching several sys_ble_channel_create() calls.
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_channel_create(const sys_ble_channel_cfg_t* cfg, bool sync_now);

/**
 * @brief Initialize the BLE Manager abstraction layer and register connection callbacks.
 *
 * @return err_h Status report (NULL on success, or Critical/Warning error status).
 */
err_h sys_ble_init(void);

/**
 * @brief Add a callback route for a BLE event.
 *
 * @param on_event The event to route.
 * @param route_mask The callback route mask.
 * @return err_h Status report.
 */
err_h sys_ble_add_callback(sys_ble_events_e on_event, uint16_t route_mask);

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
 * @param cfg Pointer to characteristic configuration containing its UUID, properties, and RX buffer.
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_char_create(uint16_t svc_uuid, const sys_ble_char_create_t* cfg);

/**
 * @brief Remove a BLE GATT characteristic from a service.
 *
 * @param svc_uuid 16-bit UUID of the parent service.
 * @param char_uuid 16-bit UUID of the characteristic to remove.
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_char_remove(uint16_t svc_uuid, uint16_t char_uuid);

/**
 * @brief Assign and allocate a TX ring buffer for a characteristic.
 *
 * TX ring buffers allow non-blocking queueing of outgoing BLE indications and notifications.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param buf_cfg Pointer to TX buffer configuration (header, size, indicate/notify).
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_char_assign_tx_buffer(uint16_t char_uuid, const sys_ble_tx_buf_cfg_t* buf_cfg);

/**
 * @brief Get the RX semaphore handle associated with a characteristic.
 *
 * This binary semaphore is given by the manager whenever a peer writes data to the characteristic.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param out_sem Pointer to store the FreeRTOS semaphore handle.
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_char_get_rx_semaphore(uint16_t char_uuid, SemaphoreHandle_t* out_sem);

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
 * @brief Test/debug utility: inject raw bytes into a characteristic's RX buffer
 * as if a peer had written them.
 *
 * Takes the exact same push-then-signal path as a real GATT write
 * (sys_ble_gatt_access_cb()'s BLE_GATT_ACCESS_OP_WRITE_CHR branch), so it
 * exercises the full RX pipeline - ring buffer, semaphore, and any consumer
 * bound via sys_ble_char_rx_dequeue() or sys_interface_bind_ble_rx() - without
 * needing a connected peer.
 *
 * @param char_uuid 16-bit UUID of the characteristic (must have rx_buffer_size > 0).
 * @param data Pointer to the raw bytes to inject.
 * @param len Length of the raw bytes.
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_char_rx_inject(uint16_t char_uuid, const uint8_t* data, size_t len);

/**
 * @brief Send data by enqueuing it into a characteristic's TX ring buffer.
 *
 * The background BLE task will dequeue this data and transmit it as notification or indication.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param header Header byte identifying which TX slot to enqueue into (see sys_ble_char_assign_tx_buffer()).
 * @param data Pointer to the data payload to send.
 * @param len Length of the data payload.
 * @param return_when_full If true, returns immediately if the buffer is full (non-blocking).
 *                          If false, blocks for up to 100ms waiting for space.
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_char_send(uint16_t char_uuid, uint8_t header, const uint8_t* data, size_t len, bool return_when_full);

/**
 * @brief Compile the GATT database definitions and synchronize with the NimBLE host stack.
 *
 * Must be called after creating or removing services and characteristics to apply changes.
 *
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_database_sync(void);

/**
 * @brief Get the current BLE status (connection state, MTU size, overflow counts, last errors).
 *
 * @param out_status Pointer to status structure to populate.
 * @return err_h Status report (NULL on success, or error status).
 */
err_h sys_ble_get_status(sys_ble_status_t* out_status);
