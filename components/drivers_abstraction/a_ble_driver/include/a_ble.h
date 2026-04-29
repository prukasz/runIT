#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include <esp_log.h>

/** @brief Initialize the BLE driver
 *  @return ESP_OK on success, otherwise an error code
 */
esp_err_t a_ble_init(void);

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

/** @brief Register a callback function to be called when a write is received on the RX characteristic
 *  @param callback The callback function to register
 *  @return ESP_OK on success, otherwise an error code
 */
void a_ble_add_callback_on_write(esp_err_t (*callback)(const uint8_t* data, size_t len));

/** @brief Get the connection handle for the VM OUT characteristic
 *  @return The connection handle, or 0 if not connected
 */
uint16_t a_ble_get_vm_out_conn_handle(void);

/** @brief Get the value handle for the VM OUT characteristic
 *  @return The value handle, or 0 if not found
 */
uint16_t a_ble_get_vm_out_val_handle(void);