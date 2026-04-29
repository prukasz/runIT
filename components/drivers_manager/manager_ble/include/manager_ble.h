#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/ringbuf.h>
#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>
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
}m_ble_cfg_t;

#define MAX_TX_BUFFERS 3

/**
 * @brief Initializes the BLE manager task and synchronization primitives
 * @param events_to_set Event group handle for setting bits (e.g. when RX data is received, tx is done)
 * @param events_to_wait Event group handle for waiting on bits (e.g. when data is available to transmit)
 * @param bits_tx_start Bit mask for starting TX operations
 * @param bits_tx_done Bit mask for indicating TX completion
 * @param bits_rx_received Bit mask for indicating RX data reception
 * @param task_priority Task priority for the BLE manager task
 */
void m_ble_init(m_ble_cfg_t *config);

/**
 * @brief Sets the RX buffer for incoming BLE data
 * @param rx_buffer The ring buffer handle to use for RX data
 * @note there is only one RX buffer
 */
void m_ble_buff_register_rx(RingbufHandle_t rx_buffer);


/**
 * @brief Registers a TX buffer for outgoing BLE data with a specific priority
 * @param tx_buffer The ring buffer handle to use for TX data
 * @param priority The priority level for this TX buffer (0 = highest)
 * @note up to MAX_TX_BUFFERS can be registered no buffs with same priority
 */

void m_ble_buff_register_tx(RingbufHandle_t tx_buffer, uint8_t priority);

/**
 * @brief Enqueues data into a specific TX buffer and wakes up the BLE task
 * @param tx_buffer The ring buffer to enqueue data into
 * @param data The data to enqueue
 * @param len The length of the data
 * @return ESP_OK on success, otherwise an error code
 */
esp_err_t m_ble_tx_enqueue(RingbufHandle_t tx_buffer, const uint8_t* data, size_t len);

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
 * @param len The length of the data to dequeue
 * @return ESP_OK on success, otherwise an error code
 */
esp_err_t m_ble_rx_dequeue(uint8_t* data, size_t* len);

