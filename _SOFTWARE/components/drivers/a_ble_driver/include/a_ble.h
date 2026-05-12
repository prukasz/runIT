#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/ringbuf.h>


typedef struct{
    EventBits_t bit_indication_complete;
    EventBits_t bit_notify_complete;
    EventBits_t bit_indication_timeout;
    EventBits_t bit_connected;
    EventBits_t bit_connection_failed;
    EventBits_t bit_disconnected;
    EventBits_t bit_mtu_update;
    EventBits_t bit_rx_received;
    EventBits_t bit_rx_failed;
}a_ble_host_bits_t;

typedef struct{
    TaskHandle_t manager_task_handle;
    TaskHandle_t supervisor_task_handle;
    EventGroupHandle_t event_group;
    a_ble_host_bits_t bits;
}a_ble_host_cfg_t;

/** @brief Add a receive buffer to the BLE driver
 *  @param rb The ring buffer to add
 */
void a_ble_add_rx_buffer(RingbufHandle_t rb);

/** @brief Initialize the BLE driver
 *  @return ESP_OK on success, otherwise an error code
 *  @param events A struct containing event group handles and bit definitions for BLE events
 */
esp_err_t a_ble_init(a_ble_host_cfg_t *events_cfg);

/** @brief Set the name of the BLE device
 *  @param name The name to set
 *  @return ESP_OK on success, otherwise an error code
 */
esp_err_t a_ble_set_name(const char* name);

/** @brief Send a notification to a connected client
 *  @param conn_handle The connection handle
 *  @param chr_val_handle The characteristic value handle
 *  @param data The data to send
 *  @param len The length of the data
 *  @return ESP_OK on success, otherwise an error code
 */
esp_err_t a_ble_send_notification(uint16_t conn_handle, uint16_t chr_val_handle, const uint8_t *data, size_t len);

/** @brief Send an indication to a connected client
 *  @param conn_handle The connection handle
 *  @param chr_val_handle The characteristic value handle
 *  @param data The data to send
 *  @param len The length of the data
 *  @return ESP_OK on success, otherwise an error code
 */
esp_err_t a_ble_send_indication(uint16_t conn_handle, uint16_t chr_val_handle, const uint8_t *data, size_t len);


/** @brief Get the connection handle for the tx characteristic
 *  @return The connection handle, or 0 if not connected
 */
extern uint16_t a_ble_get_tx_conn_handle(void);

/** @brief Get the value handle for the tx characteristic
 *  @return The value handle, or 0 if not found
 */
extern uint16_t a_ble_get_tx_val_handle(void);

/** @brief Get the current MTU size
 *  @return The current MTU size
 */
uint16_t a_ble_get_mtu();