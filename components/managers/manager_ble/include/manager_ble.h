#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/ringbuf.h>
#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <freertos/task.h>  

typedef struct{
    EventGroupHandle_t m_ble_events;
    EventBits_t m_ble_bits_tx_start;
    EventBits_t m_ble_bits_tx_done;
    EventBits_t m_ble_bits_on_rx; 
    EventBits_t m_ble_bits_on_connect;
    EventBits_t m_ble_bits_on_disconnect;
    EventBits_t m_ble_bits_on_mtu_update;
    EventBits_t m_ble_bits_on_connection_failed;
    EventBits_t m_ble_bits_on_indication_timeout;
    EventBits_t m_ble_bits_on_indication_complete;
    EventBits_t m_ble_bits_on_notify_complete;
    uint8_t task_priority;
    uint32_t task_stack_size;
} m_ble_cfg_t;

#define MAX_TX_BUFFERS 3

/**
 * @brief Initializes the BLE manager task and synchronization primitives
 * @param config Pointer to the configuration struct containing event group and bit definitions
 */
void m_ble_init(m_ble_cfg_t *config);

/**
 * @brief Sets the RX buffer for incoming BLE data
 * @param rx_buffer The ring buffer handle to use for RX data (Should be RINGBUF_TYPE_NOSPLIT)
 * @note there is only one RX buffer
 */
void m_ble_buff_register_rx(RingbufHandle_t rx_buffer);

/**
 * @brief Registers a TX buffer for outgoing BLE data with a specific priority and packing rules
 * @param tx_buffer The ring buffer handle to use for TX data
 * @param buff_type The type of ring buffer (NOSPLIT for messages, BYTEBUF for data streams)
 * @param auto_pack Set to true to automatically pack data up to MTU size (requires BYTEBUF)
 * @param auto_pack_header A 1-byte header to prepend to auto-packed streams
 * @param item_size The exact size in bytes of the structs being packed (to prevent slicing)
 * @param priority The priority level for this TX buffer (0 = highest)
 * @note up to MAX_TX_BUFFERS can be registered. No buffs with same priority.
 */
void m_ble_buff_register_tx(RingbufHandle_t tx_buffer, RingbufferType_t buff_type, bool auto_pack, uint8_t auto_pack_header, size_t item_size, uint8_t priority);

/**
 * @brief Enqueues data into a specific TX buffer and wakes up the BLE task
 * @param tx_buffer The ring buffer to enqueue data into
 * @param data The data to enqueue
 * @param len The length of the data
 * @param return_when_full If true, wait up to 100ms for space. If false, fail immediately if full.
 * @return ESP_OK on success, otherwise an error code
 */
esp_err_t m_ble_tx_enqueue(RingbufHandle_t tx_buffer, const uint8_t* data, size_t len, bool return_when_full);

/**
 * @brief Enqueues received data into the RX buffer, set Received event bit
 * @param data The data to enqueue
 * @param len The length of the data
 * @return ESP_OK on success, otherwise an error code
 */
esp_err_t m_ble_rx_enqueue(const uint8_t* data, size_t len);

/**
 * @brief Dequeues data from the RX buffer
 * @param data The buffer to store the dequeued data
 * @param len Pointer to store the length of the dequeued data
 * @return ESP_OK on success, otherwise an error code
 */
esp_err_t m_ble_rx_dequeue(uint8_t* data, size_t* len);

/**
 * @brief Dequeues and optionally packs data from a priority TX buffer up to the MTU limit
 * @param priority_idx The priority index of the registered buffer (0 to MAX_TX_BUFFERS-1)
 * @param data Pointer to the output buffer to hold the dequeued payload
 * @param len Pointer to store the final length of the payload
 * @param max_payload The maximum allowable bytes to pack (usually MTU - 3)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if empty, or other error code
 */
esp_err_t m_ble_tx_dequeue(uint8_t priority_idx, uint8_t* data, size_t* len, size_t max_payload);