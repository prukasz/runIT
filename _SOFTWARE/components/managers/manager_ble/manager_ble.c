#include "manager_ble.h"
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <string.h>
#include "esp_timer.h"


#define TAG __FILE_NAME__

typedef struct{
    RingbufHandle_t tx_buff;
    RingbufferType_t buff_type;
    size_t item_size; //const size of items in the buffer, used for auto-packing
    bool auto_pack;
    uint8_t auto_pack_header;
}_tx_buff_slot_t;


/*****************************************************************************************/
/*Global Variables*/
static m_ble_cfg_t *cfg = NULL;

static RingbufHandle_t m_ble_rx_buffer = NULL;

// tx_buffers storage
static _tx_buff_slot_t m_ble_tx_buffers[MAX_TX_BUFFERS] = {0};

/*****************************************************************************************/

/**
 * @brief This task is responsible for sending BLE data from assigned buffers
 */

static void m_ble_task(void *pvParameters) {
    (void)pvParameters;
    while (1) {
        // Wait for a signal that data has been queued
        xEventGroupWaitBits(
            cfg->ble_cfg.event_group,
            cfg->m_ble_bits_tx_start,
            pdTRUE,  
            pdFALSE,
            portMAX_DELAY);

        bool data_sent;
        /*DEBUG*/
        uint64_t start_time = esp_timer_get_time();
        uint32_t send_count = 0;
        /*DEBUG*/
        
        do {
            uint8_t tx_data[527] = {0};
            data_sent = false;
            
            // 1. Calculate Maximum Permissible Payload dynamically
            uint16_t current_mtu = a_ble_get_mtu();
            // Subtract 3 bytes for standard BLE GATT Notification/Indication header
            size_t max_payload = (current_mtu > 3) ? (current_mtu - 3) : 20; 
            
            // Cap at local buffer size just in case MTU is unusually large
            if (max_payload > sizeof(tx_data)) {
                max_payload = sizeof(tx_data);
            }

            // Iterate through buffers, highest priority (0) first
            for (int i = 0; i < MAX_TX_BUFFERS; i++) {
                if (m_ble_tx_buffers[i].tx_buff == NULL) continue;

                size_t tx_len = 0;
                
                // 2. Call updated dequeue passing priority index and max_payload limit
                esp_err_t deq_res = m_ble_tx_dequeue(i, tx_data, &tx_len, max_payload);
                
                if (deq_res == ESP_OK && tx_len > 0) {
                    uint16_t conn_handle = a_ble_get_tx_conn_handle();
                    uint16_t val_handle = a_ble_get_tx_val_handle();
                    
                    REPEAT:
                    esp_err_t send_res = a_ble_send_notification(conn_handle, val_handle, tx_data, tx_len); 
                    if (send_res == ESP_OK) { 
                        data_sent = true;
                        /*DEBUG*/
                        send_count++;
                        /*DEBUG*/
                        break; 
                    } else if (send_res == ESP_ERR_NO_MEM) {
                        // BLE controller ran out of memory, wait for TX complete events
                        xEventGroupWaitBits(
                            cfg->ble_cfg.event_group,
                            cfg->ble_cfg.bits.bit_notify_complete | cfg->ble_cfg.bits.bit_indication_complete | cfg->ble_cfg.bits.bit_indication_timeout,
                            pdTRUE,  
                            pdFALSE,
                            pdMS_TO_TICKS(100)
                        );
                        goto REPEAT; // Retry sending the same data
                    } else {
                        if (send_res != 6) {
                            ESP_LOGE(TAG, "Fatal send error: %s", esp_err_to_name(send_res));
                        }
                        break; 
                    }
                }
            }
        } while (data_sent); // Keep looping until ALL buffers are completely empty
        
        /*DEBUG*/
        ESP_LOGI(TAG, "BLE Task: Completed sending %lu packets of data in %lld us", send_count, (esp_timer_get_time() - start_time));
        xEventGroupSetBits(cfg->ble_cfg.event_group, cfg->m_ble_bits_tx_done);\
        xTaskNotifyGive(cfg->ble_cfg.supervisor_task_handle); // Notify supervisor that TX batch is done
    }
}

esp_err_t m_ble_init(m_ble_cfg_t *config) {
    cfg = config;
    ESP_LOGI(TAG, "Starting BLE manager task...");
    xTaskCreate(&m_ble_task, "m_ble_task", cfg->task_stack_size, NULL, cfg->task_priority, &cfg->manager_task_handle);
    ESP_LOGI(TAG, "Initializing BLE stack...");

    esp_err_t err = a_ble_init(&(cfg->ble_cfg));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE driver: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "BLE driver and manager initialized successfully, please register buffers now ->");
    return ESP_OK;
}

void m_ble_buff_register_rx(RingbufHandle_t rx_buffer) {
    m_ble_rx_buffer = rx_buffer;
    a_ble_add_rx_buffer(rx_buffer);
    ESP_LOGI(TAG, "Registered RX buffer with BLE driver");
}

void m_ble_buff_register_tx(RingbufHandle_t tx_buffer, RingbufferType_t buff_type, bool auto_pack, uint8_t auto_pack_header, size_t item_size, uint8_t priority) {
    if (priority < MAX_TX_BUFFERS) {
        m_ble_tx_buffers[priority].tx_buff = tx_buffer;
        m_ble_tx_buffers[priority].buff_type = buff_type;
        m_ble_tx_buffers[priority].auto_pack = auto_pack;
        m_ble_tx_buffers[priority].auto_pack_header = auto_pack_header;
        m_ble_tx_buffers[priority].item_size = item_size;
        ESP_LOGI(TAG, "Registered TX buffer with priority %u", priority);
        return;
    }
    ESP_LOGE(TAG, "Invalid buffer priority: %u", priority);
}


esp_err_t m_ble_tx_enqueue(RingbufHandle_t tx_buffer, const uint8_t* data, size_t len, bool return_when_full) { 
    if (tx_buffer == NULL) { return ESP_ERR_INVALID_ARG;}

    uint32_t wait_time_ms = return_when_full ? 100 : 0;

    //add data to ringbuffer, if fails return error
    if (xRingbufferSend(tx_buffer, data, len, pdMS_TO_TICKS(wait_time_ms)) != pdTRUE) {
         return ESP_ERR_NO_MEM;
    }
    //signal manager task that data is in buffer
    xEventGroupSetBits(cfg->ble_cfg.event_group, cfg->m_ble_bits_tx_start);
    return ESP_OK; 
}


esp_err_t m_ble_rx_enqueue(const uint8_t* data, size_t len) {
    if (m_ble_rx_buffer == NULL) { return ESP_ERR_INVALID_ARG;}

    //add data to ringbuffer, if fails return error
    if (xRingbufferSend(m_ble_rx_buffer, data, len, 0) != pdTRUE) {
         return ESP_ERR_NO_MEM;
    }
    //signal other task that data is in buffer
    xEventGroupSetBits(cfg->ble_cfg.event_group, cfg->ble_cfg.bits.bit_rx_received);
    //notify supervisor task 
    xTaskNotifyGive(cfg->ble_cfg.supervisor_task_handle);
    return ESP_OK;
}

esp_err_t m_ble_rx_dequeue(uint8_t* data, size_t* len) {
    if (m_ble_rx_buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t item_size = 0;
    void *item = xRingbufferReceive(m_ble_rx_buffer, &item_size, 0);
    if (item == NULL) { return ESP_ERR_NOT_FOUND; }

    memcpy(data, item, item_size);
    *len = item_size;

    vRingbufferReturnItem(m_ble_rx_buffer, item);
    return ESP_OK;
}

// Note: Update your header file to match this new signature if you have one.
esp_err_t m_ble_tx_dequeue(uint8_t priority_idx, uint8_t* data, size_t* len, size_t max_payload) {
    if (priority_idx >= MAX_TX_BUFFERS) return ESP_ERR_INVALID_ARG;
    
    _tx_buff_slot_t *slot = &m_ble_tx_buffers[priority_idx];
    if (slot->tx_buff == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* ------------------------------------------------------------------
     * SCENARIO A: Standard Message (NOSPLIT)
     * Grab exactly one item. If it exceeds MTU, cap it to prevent overflow.
     * ------------------------------------------------------------------ */
    if (slot->buff_type == RINGBUF_TYPE_NOSPLIT) {
        size_t item_size = 0;
        void *item = xRingbufferReceive(slot->tx_buff, &item_size, 0);
        if (item == NULL) { return ESP_ERR_NOT_FOUND; }

        size_t copy_len = (item_size > max_payload) ? max_payload : item_size;
        memcpy(data, item, copy_len);
        *len = copy_len;
        
        vRingbufferReturnItem(slot->tx_buff, item);
        
        if (item_size > max_payload) {
            ESP_LOGW(TAG, "NOSPLIT item truncated from %zu to %zu bytes (MTU limit)", item_size, copy_len);
        }
        return ESP_OK;
    } 
    
    /* ------------------------------------------------------------------
     * SCENARIO B: Auto-Packing Stream (BYTEBUF)
     * Calculate whole structs, attach header, and pack up to MTU.
     * ------------------------------------------------------------------ */
    else if (slot->buff_type == RINGBUF_TYPE_BYTEBUF) {
        
        // Safety check: Ensure valid config for packing
        if (!slot->auto_pack || slot->item_size == 0) {
            ESP_LOGE(TAG, "BYTEBUF lacks auto_pack config or item_size is 0");
            return ESP_ERR_INVALID_STATE;
        }

        // 1. Calculate how many WHOLE structs fit into our payload 
        // (-1 byte to leave room for the header)
        size_t space_for_structs = max_payload - 1; 
        size_t max_structs = space_for_structs / slot->item_size;
        
        if (max_structs == 0) {
            ESP_LOGE(TAG, "MTU payload (%zu) too small for even one struct (%zu)!", space_for_structs, slot->item_size);
            return ESP_ERR_INVALID_SIZE;
        }

        // 2. Define our exact target byte count to prevent slicing
        size_t bytes_to_pull = max_structs * slot->item_size;
        size_t current_offset = 1; // Start at 1 to leave room for data[0] header
        bool got_data = false;

        // 3. Drain the buffer up to our calculated struct limit
        while ((current_offset - 1) < bytes_to_pull) {
            size_t item_size = 0;
            size_t space_left = bytes_to_pull - (current_offset - 1);
            
            // Fetch contiguous chunk
            void *item = xRingbufferReceiveUpTo(slot->tx_buff, &item_size, 0, space_left);
            if (item == NULL) {
                break; // Queue is empty, stop pulling
            }

            got_data = true;
            memcpy(&data[current_offset], item, item_size);
            current_offset += item_size;
            
            vRingbufferReturnItem(slot->tx_buff, item);
        }

        if (!got_data) {
            return ESP_ERR_NOT_FOUND; 
        }

        // 4. Attach the header byte to the very beginning
        data[0] = slot->auto_pack_header;
        *len = current_offset; // Total size is data + 1 byte header
        
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}