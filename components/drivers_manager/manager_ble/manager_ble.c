#include "manager_ble.h"
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <string.h>

#include "a_ble.h"
#include "gatt_svc.h"

#define TAG __FILE_NAME__

/*****************************************************************************************/

TaskHandle_t m_ble_task_handle = NULL;

static m_ble_cfg_t *cfg = NULL;

static RingbufHandle_t m_ble_rx_buffer = NULL;
static RingbufHandle_t m_ble_tx_buffers[MAX_TX_BUFFERS] = {0};

/*****************************************************************************************/

static esp_err_t m_ble_rb_enqueue(RingbufHandle_t rb, const uint8_t *data, size_t len) {
    if (xRingbufferSend(rb, data, len, pdMS_TO_TICKS(100)) != pdTRUE) { return ESP_ERR_NO_MEM; }
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
    while (1) {
        // Wait for a signal that data has been queued
        xEventGroupWaitBits(
            cfg->m_ble_events,
            cfg->m_ble_bits_tx_start,
            pdTRUE,  
            pdFALSE,
            portMAX_DELAY);

        bool data_sent;
        uint64_t start_time = esp_timer_get_time();
        uint32_t send_count = 0;
        do {
            uint8_t tx_data[512] = {0};
            data_sent = false;
            // Iterate through buffers, highest priority (0) first
            for (int i = 0; i < MAX_TX_BUFFERS; i++) {
                if (m_ble_tx_buffers[i] == NULL) continue;

                size_t tx_len = sizeof(tx_data);
                esp_err_t deq_res = m_ble_rb_dequeue(m_ble_tx_buffers[i], tx_data, &tx_len);
                
                if (deq_res == ESP_OK) {
                    uint16_t conn_handle = a_ble_get_tx_conn_handle();
                    uint16_t val_handle = a_ble_get_tx_val_handle();
                    
                    REPEAT:
                    esp_err_t send_res = a_ble_send_notification(conn_handle, val_handle, tx_data, tx_len); 
                    if (send_res == ESP_OK) { 
                        data_sent = true;
                        send_count++;
                        break; 
                    }else if (send_res == ESP_ERR_NO_MEM) {
                        xEventGroupWaitBits(
                            cfg->m_ble_events,
                            cfg->m_ble_bits_on_notify_complete | cfg->m_ble_bits_on_indication_complete | cfg->m_ble_bits_on_indication_timeout,
                            pdTRUE,  
                            pdFALSE,
                            pdMS_TO_TICKS(100)
                        );
                            goto REPEAT; // Retry sending the same data after being notified of completion or timeout
                    } else {
                        if (send_res != 6) {
                            ESP_LOGE(TAG, "Fatal send error: %s", esp_err_to_name(send_res));
                        }
                        break; 
                    }
                }
            }
        } while (data_sent); // Keep looping until ALL buffers are completely empty

        ESP_LOGI(TAG, "BLE Task: Completed sending data cnt %lu queued data in %lld us", send_count, (esp_timer_get_time() - start_time));
        xEventGroupSetBits(cfg->m_ble_events, cfg->m_ble_bits_tx_done);
    }
}

void m_ble_init(m_ble_cfg_t *config) {
    //assign given config 
    cfg = config;

    xTaskCreate(&m_ble_task, "m_ble_task", cfg->task_stack_size, NULL, cfg->task_priority, &m_ble_task_handle);
    ESP_LOGI(TAG, "Initializing BLE stack...");
    a_ble_events_t ble_events = {
        .ble_manager_task_handle = m_ble_task_handle, 
        .ble_event = cfg->m_ble_events,
        .bits_indication_complete = cfg->m_ble_bits_on_indication_complete,
        .bits_notify_complete = cfg->m_ble_bits_on_notify_complete,
        .bits_indication_timeout = cfg->m_ble_bits_on_indication_timeout,
        .bits_connected = cfg->m_ble_bits_on_connect,
        .bits_connection_failed = cfg->m_ble_bits_on_connection_failed,
        .bits_mtu_update = cfg->m_ble_bits_on_mtu_update,
        .bits_rx_received = cfg->m_ble_bits_on_rx 
    };
    //init BLE driver with events struct containing event group handle and bit definitions for events
    esp_err_t err = a_ble_init(&ble_events);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE driver: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "BLE driver and manager initialized successfully, register buffers now->");
}

void m_ble_buff_register_rx(RingbufHandle_t rx_buffer) {
    m_ble_rx_buffer = rx_buffer;
    a_ble_add_rx_buffer(rx_buffer);
    ESP_LOGI(TAG, "Registered RX buffer with BLE driver");
}

void m_ble_buff_register_tx(RingbufHandle_t tx_buffer, uint8_t priority) {
    if (priority < MAX_TX_BUFFERS) {
        m_ble_tx_buffers[priority] = tx_buffer;
        ESP_LOGI(TAG, "Registered TX buffer with priority %u", priority);
        return;
    }
    ESP_LOGE(TAG, "Invalid buffer priority: %u", priority);
}


esp_err_t m_ble_tx_enqueue(RingbufHandle_t tx_buffer, const uint8_t* data, size_t len) { 
    if (tx_buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t res = m_ble_rb_enqueue(tx_buffer, data, len);
    if (res == ESP_OK) {
        xEventGroupSetBits(cfg->m_ble_events, cfg->m_ble_bits_tx_start);
    }
    return res;
}


esp_err_t m_ble_rx_enqueue(const uint8_t* data, size_t len) {
    if (m_ble_rx_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t res = m_ble_rb_enqueue(m_ble_rx_buffer, data, len);
    if (res == ESP_OK) {
        xEventGroupSetBits(cfg->m_ble_events, cfg->m_ble_bits_on_rx);
    }
    return res;
}

esp_err_t m_ble_rx_dequeue(uint8_t* data, size_t* len) {
    if (m_ble_rx_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return m_ble_rb_dequeue(m_ble_rx_buffer, data, len);
}