#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/ringbuf.h>
#include <freertos/semphr.h>
#include "status.h"

#define MAX_TX_BUFFERS 4

typedef enum { RINGBUF_TYPE_NOSPLIT_BUF = RINGBUF_TYPE_NOSPLIT, RINGBUF_TYPE_BYTE_BUF = RINGBUF_TYPE_BYTEBUF } sys_ble_ringbuf_type_e;

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
  uint8_t buffer_id;
  RingbufferType_t buff_type;  // RINGBUF_TYPE_NOSPLIT or RINGBUF_TYPE_BYTEBUF
  size_t size;
  bool auto_pack;
  uint8_t header;
  size_t item_size;
  uint8_t priority;
  bool is_indication;
} sys_ble_tx_buf_cfg_t;

typedef void (*sys_ble_connect_cb_t)(uint16_t conn_handle);
typedef void (*sys_ble_disconnect_cb_t)(uint16_t conn_handle, int reason);
typedef void (*sys_ble_failure_cb_t)(esp_err_t error_code);

typedef struct {
  sys_ble_connect_cb_t on_connect;
  sys_ble_disconnect_cb_t on_disconnect;
  sys_ble_failure_cb_t on_failure;
} sys_ble_callbacks_t;

typedef struct {
  uint8_t buffer_id;
  RingbufHandle_t tx_buff;
  RingbufferType_t buff_type;
  size_t item_size;
  bool auto_pack;
  uint8_t header;
  uint8_t priority;
  bool is_indication;
} sys_ble_tx_slot_t;

typedef struct {
  bool is_connected;
  uint16_t mtu_size;
  uint32_t rx_overflow_count;
  esp_err_t last_error;
} sys_ble_status_t;

/**
 * @brief Initialize the BLE Manager abstraction layer and register connection callbacks.
 *
 * @param callbacks Pointer to structure containing connection event callbacks.
 * @return status_rep_t Status report (STA_OK on success, or Critical/Warning error status).
 */
status_rep_t sys_ble_init(const sys_ble_callbacks_t* callbacks);

/**
 * @brief Create and register a new BLE GATT service config in the manager.
 *
 * @param cfg Pointer to service configuration struct containing the service UUID.
 * @return status_rep_t Status report (STA_OK on success, or error status).
 */
status_rep_t sys_ble_service_create(const sys_ble_svc_cfg_t* cfg);

/**
 * @brief Remove a BLE GATT service from the manager.
 *
 * If the driver is running and the service is registered with NimBLE, it will be
 * dynamically removed from the stack immediately.
 *
 * @param svc_uuid 16-bit UUID of the service to remove.
 * @return status_rep_t Status report (STA_OK on success, or error status).
 */
status_rep_t sys_ble_service_remove(uint16_t svc_uuid);

/**
 * @brief Create a new BLE GATT characteristic under a parent service.
 *
 * @param svc_uuid 16-bit UUID of the parent service.
 * @param cfg Pointer to characteristic configuration containing its UUID, properties, and RX buffer.
 * @return status_rep_t Status report (STA_OK on success, or error status).
 */
status_rep_t sys_ble_char_create(uint16_t svc_uuid, const sys_ble_char_create_t* cfg);

/**
 * @brief Remove a BLE GATT characteristic from a service.
 *
 * @param svc_uuid 16-bit UUID of the parent service.
 * @param char_uuid 16-bit UUID of the characteristic to remove.
 * @return status_rep_t Status report (STA_OK on success, or error status).
 */
status_rep_t sys_ble_char_remove(uint16_t svc_uuid, uint16_t char_uuid);

/**
 * @brief Assign and allocate a TX ring buffer for a characteristic.
 *
 * TX ring buffers allow non-blocking queueing of outgoing BLE indications and notifications.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param buf_cfg Pointer to TX buffer configuration (size, priority, type, header, etc.).
 * @return status_rep_t Status report (STA_OK on success, or error status).
 */
status_rep_t sys_ble_char_assign_tx_buffer(uint16_t char_uuid, const sys_ble_tx_buf_cfg_t* buf_cfg);

/**
 * @brief Get the RX semaphore handle associated with a characteristic.
 *
 * This binary semaphore is given by the manager whenever a peer writes data to the characteristic.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param out_sem Pointer to store the FreeRTOS semaphore handle.
 * @return status_rep_t Status report (STA_OK on success, or error status).
 */
status_rep_t sys_ble_char_get_rx_semaphore(uint16_t char_uuid, SemaphoreHandle_t* out_sem);

/**
 * @brief Dequeue incoming data written by a peer to a characteristic's RX buffer.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param buffer Target destination buffer to copy the dequeued data into.
 * @param max_len Maximum length of data to copy.
 * @param out_len Pointer to store the actual size of the dequeued data.
 * @return status_rep_t Status report (STA_OK on success, or error status).
 */
status_rep_t sys_ble_char_rx_dequeue(uint16_t char_uuid, uint8_t* buffer, size_t max_len, size_t* out_len);

/**
 * @brief Send data by enqueuing it into a characteristic's TX ring buffer.
 *
 * The background BLE task will dequeue this data and transmit it as notification or indication.
 *
 * @param char_uuid 16-bit UUID of the characteristic.
 * @param buffer_id ID of the buffer slot (typically 0).
 * @param data Pointer to the data payload to send.
 * @param len Length of the data payload.
 * @param return_when_full If true, returns immediately if the buffer is full (non-blocking).
 *                          If false, blocks for up to 100ms waiting for space.
 * @return status_rep_t Status report (STA_OK on success, or error status).
 */
status_rep_t sys_ble_char_send(uint16_t char_uuid, uint8_t buffer_id, const uint8_t* data, size_t len, bool return_when_full);

/**
 * @brief Compile the GATT database definitions and synchronize with the NimBLE host stack.
 *
 * Must be called after creating or removing services and characteristics to apply changes.
 *
 * @return status_rep_t Status report (STA_OK on success, or error status).
 */
status_rep_t sys_ble_database_sync(void);

/**
 * @brief Get the current BLE status (connection state, MTU size, overflow counts, last errors).
 *
 * @param out_status Pointer to status structure to populate.
 * @return status_rep_t Status report (STA_OK on success, or error status).
 */
status_rep_t sys_ble_get_status(sys_ble_status_t* out_status);
