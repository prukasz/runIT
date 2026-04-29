#include "manager_ble.h"
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <string.h>

#include "a_ble.h"
#include "gatt_svc.h"

#define TAG "M_BLE"

/*****************************************************************************************/
/* Private variables */
TaskHandle_t m_ble_task_handle = NULL;
static EventGroupHandle_t m_ble_events_to_set = NULL;
static EventGroupHandle_t m_ble_events_to_wait = NULL;
static uint32_t m_ble_bits_start = 0;
static uint32_t m_ble_bits_done = 0;
static uint32_t m_ble_bits_rx_received = 0; 

static RingbufHandle_t m_ble_rx_buffer = NULL;
static RingbufHandle_t m_ble_tx_buffers[MAX_TX_BUFFERS] = {0};
/*****************************************************************************************/

static esp_err_t m_ble_rb_enqueue(RingbufHandle_t rb, const uint8_t *data, size_t len) {
    if (xRingbufferSend(rb, data, len, 0) != pdTRUE) { return ESP_ERR_NO_MEM; }
    return ESP_OK;
}

static esp_err_t m_ble_rb_dequeue(RingbufHandle_t rb, uint8_t *data, size_t *len) {
    size_t item_size = 0;
    void *item = xRingbufferReceive(rb, &item_size, 0);
    if (item == NULL) { return ESP_ERR_NOT_FOUND; }

    if (item_size > *len) {
        vRingbufferReturnItem(rb, item);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(data, item, item_size);
    *len = item_size;

    vRingbufferReturnItem(rb, item);
    return ESP_OK;
}


static void m_ble_task(void *pvParameters) {
    (void)pvParameters;
    uint8_t tx_data[512];

    while (1) {
        // Wait for a signal that data has been queued
        xEventGroupWaitBits(
            m_ble_events_to_wait,
            m_ble_bits_start,
            pdTRUE,  
            pdFALSE,
            portMAX_DELAY);

        bool data_sent;
        do {
            data_sent = false;

            // Iterate through buffers, highest priority (0) first
            for (int i = 0; i < MAX_TX_BUFFERS; i++) {
                if (m_ble_tx_buffers[i] == NULL) continue;

                size_t tx_len = sizeof(tx_data);
                esp_err_t deq_res = m_ble_rb_dequeue(m_ble_tx_buffers[i], tx_data, &tx_len);
                
                if (deq_res == ESP_OK) {
                    uint16_t conn_handle = gatt_get_vm_out_conn_handle();
                    uint16_t val_handle = gatt_get_vm_out_val_handle();
                    
                    esp_err_t send_res = a_ble_send_notification(conn_handle, val_handle, tx_data, tx_len);
                    if (send_res != ESP_OK) {
                        ESP_LOGW(TAG, "a_ble_send_notification failed: %d", send_res);
                    }
                    
                    data_sent = true;
                    break; 
                }
            }
        } while (data_sent); // Keep looping until ALL buffers are completely empty

        if (m_ble_events_to_set != NULL) {
            xEventGroupSetBits(m_ble_events_to_set, m_ble_bits_done);
        }
    }
}





void m_ble_init(EventGroupHandle_t events_to_set, EventGroupHandle_t events_to_wait, uint32_t bits_tx_start, uint32_t bits_tx_done, uint32_t bits_rx_received, UBaseType_t task_priority) {
    m_ble_events_to_set = events_to_set;
    m_ble_events_to_wait = events_to_wait;
    m_ble_bits_start = bits_tx_start;
    m_ble_bits_done = bits_tx_done;
    m_ble_bits_rx_received = bits_rx_received;  
    a_ble_add_callback_on_write(&m_ble_rx_enqueue); 
    xTaskCreate(&m_ble_task, "m_ble_task", 4096, NULL, task_priority, &m_ble_task_handle);
}

void m_ble_buff_register_rx(RingbufHandle_t rx_buffer) {
    m_ble_rx_buffer = rx_buffer;
}

void m_ble_buff_register_tx(RingbufHandle_t tx_buffer, uint8_t priority) {
    if (priority < MAX_TX_BUFFERS) {
        m_ble_tx_buffers[priority] = tx_buffer;
    } else {
        ESP_LOGE(TAG, "Invalid buffer priority: %u", priority);
    }
}

/**
 * @brief Enqueues data into a specific TX buffer and wakes up the BLE task
 * @param tx_buffer The ring buffer to enqueue data into
 * @param data The data to enqueue
 * @param len The length of the data
 * @return ESP_OK on success, otherwise an error code
 */
esp_err_t m_ble_tx_enqueue(RingbufHandle_t tx_buffer, const uint8_t* data, size_t len) { 
    if (tx_buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t res = m_ble_rb_enqueue(tx_buffer, data, len);
    if (res == ESP_OK && m_ble_events_to_wait != NULL) {
        xEventGroupSetBits(m_ble_events_to_wait, m_ble_bits_start);
    }
    return res;
}

/**
 * @brief Enqueues received data into the RX buffer, set Received event bit
 * @param data The data to enqueue
 * @param len The length of the data
 * @return ESP_OK on success, otherwise an error code
 */
esp_err_t m_ble_rx_enqueue(const uint8_t* data, size_t len) {
    if (m_ble_rx_buffer == NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t res =  m_ble_rb_enqueue(m_ble_rx_buffer, data, len);
    if (res == ESP_OK && m_ble_events_to_set != NULL) {
        xEventGroupSetBits(m_ble_events_to_set, m_ble_bits_rx_received);
    }
    return res;
}

/**
 * @brief Dequeues data from the RX buffer
 * @param data The buffer to store the dequeued data
 * @param len The length of the data to dequeue
 * @return ESP_OK on success, otherwise an error code
 */
esp_err_t m_ble_rx_dequeue(uint8_t* data, size_t* len) {
    if (m_ble_rx_buffer == NULL) return ESP_ERR_INVALID_STATE;
    return m_ble_rb_dequeue(m_ble_rx_buffer, data, len);
}