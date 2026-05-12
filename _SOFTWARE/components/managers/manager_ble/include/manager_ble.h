#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/ringbuf.h>
#include <esp_err.h>
#include <stdint.h>
#include <freertos/task.h>  
#include "a_ble.h"

#define MAX_TX_BUFFERS 3

typedef struct{

    EventBits_t bit_on_connect;
    EventBits_t bit_on_disconnect;
    EventBits_t bit_on_connection_failed;
    EventBits_t bit_on_mtu_change;
    EventBits_t bit_on_rx_received;
    EventBits_t bit_on_rx_failed;
    EventBits_t bit_tx_start;
    EventBits_t bit_tx_done;

    TaskHandle_t manager_task_handle;
    TaskHandle_t supervisor_task_handle;
    EventGroupHandle_t event_group;

} m_ble_cfg_t;


/**
 * @brief Initializes the BLE manager task and synchronization primitives
 * @param config Pointer to the configuration struct containing event group and bit definitions
 */
esp_err_t m_ble_init(m_ble_cfg_t *config);

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
