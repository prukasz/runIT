#include "manager_ble.h"
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <string.h>
#include "esp_timer.h"
#include "rtos_utils.h"
#include "sdkconfig.h"

#define TAG __FILE_NAME__


#define BIT_ON_INDICATION_COMPLETE (1 << 0)
#define BIT_ON_INDICATION_TIMEOUT (1 << 1)
#define BIT_ON_NOTIFICATION_COMPLETE (1 << 2)
#define BIT_START_TX (1 << 3)

#define BLE_TASK_STACK_SIZE 4096


R_TASK_DEFINE(m_ble_task, BLE_TASK_STACK_SIZE);


typedef struct{
    RingbufHandle_t tx_buff;
    RingbufferType_t buff_type;
    size_t item_size; //const size of items in the buffer, used for auto-packing
    bool auto_pack;
    uint8_t header;
}_tx_buff_slot_t;


/*****************************************************************************************/
/*Global Variables*/
static m_ble_cfg_t *cfg = NULL;

static RingbufHandle_t m_ble_rx_buffer = NULL;

// tx_buffers storage
static _tx_buff_slot_t m_ble_tx_buffers[MAX_TX_BUFFERS] = {0};

static esp_err_t m_ble_tx_dequeue(uint8_t priority_idx, uint8_t* data, size_t* len, size_t max_payload);

/*****************************************************************************************/

/**
 * @brief This task is responsible for sending BLE data from assigned buffers
 */

static void m_ble_task_func(void *pvParameters) {
    (void)pvParameters;
    while (1) {
        
        R_EVENT_AWAIT_ANY(cfg->event_group, cfg->bit_tx_start, WAIT_FOREVER);
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
                        R_EVENT_AWAIT_ANY(cfg->event_group, BIT_ON_INDICATION_COMPLETE | BIT_ON_NOTIFICATION_COMPLETE | BIT_ON_INDICATION_TIMEOUT, MSEC(100));
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
        //ESP_LOGI(TAG, "BLE Task: Completed sending %lu packets of data in %lld us", send_count, (esp_timer_get_time() - start_time));
        R_EVENT_SET(cfg->event_group, cfg->bit_tx_done);
        R_NOTIFY_SEND(cfg->supervisor_task_handle, 0); // Notify supervisor that TX batch is done
    }
}



a_ble_host_cfg_t host_cfg = {0};

esp_err_t m_ble_init(m_ble_cfg_t *config) {
    cfg = config;
    ESP_LOGI(TAG, "Starting BLE manager task...");
    R_TASK_START_ON_CORE(m_ble_task, &m_ble_task_func, NULL, CONFIG_PRIORITY_BLE_MANAGER_TASK, 0);
    cfg->manager_task_handle = m_ble_task;
    ESP_LOGI(TAG, "Initializing BLE stack...");

    host_cfg.manager_task_handle = m_ble_task;
    host_cfg.supervisor_task_handle = cfg->supervisor_task_handle;
    host_cfg.event_group = cfg->event_group;
    host_cfg.host_bits.bit_on_connect = cfg->bit_on_connect;
    host_cfg.host_bits.bit_on_disconnect = cfg->bit_on_disconnect;
    host_cfg.host_bits.bit_on_connection_failed = cfg->bit_on_connection_failed;
    host_cfg.host_bits.bit_on_mtu_change = cfg->bit_on_mtu_change;
    host_cfg.host_bits.bit_on_rx_received = cfg->bit_on_rx_received;
    host_cfg.host_bits.bit_on_rx_failed = cfg->bit_on_rx_failed;
    host_cfg.tx_bits.bit_on_indication_complete = BIT_ON_INDICATION_COMPLETE;
    host_cfg.tx_bits.bit_on_indication_timeout = BIT_ON_INDICATION_TIMEOUT;
    host_cfg.tx_bits.bit_on_notification_complete = BIT_ON_NOTIFICATION_COMPLETE;

    esp_err_t err = a_ble_init(&host_cfg);
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

void m_ble_buff_register_tx(RingbufHandle_t tx_buffer, RingbufferType_t buff_type, bool auto_pack, uint8_t header, size_t item_size, uint8_t priority) {
    if (priority < MAX_TX_BUFFERS) {
        m_ble_tx_buffers[priority].tx_buff = tx_buffer;
        m_ble_tx_buffers[priority].buff_type = buff_type;
        m_ble_tx_buffers[priority].auto_pack = auto_pack;
        m_ble_tx_buffers[priority].header = header;
        m_ble_tx_buffers[priority].item_size = item_size;
        ESP_LOGI(TAG, "Registered TX buffer with priority %u", priority);
        return;
    }
    ESP_LOGE(TAG, "Invalid buffer priority: %u", priority);
}


esp_err_t m_ble_tx_enqueue(RingbufHandle_t tx_buffer, const uint8_t* data, size_t len, bool return_when_full) { 
    
    uint32_t wait_time_ms = return_when_full ? 100 : 0;

    if (xRingbufferSend(tx_buffer, data, len, pdMS_TO_TICKS(wait_time_ms)) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }
    R_EVENT_SET(cfg->event_group, cfg->bit_tx_start);
    return ESP_OK; 
}


static esp_err_t m_ble_tx_dequeue(uint8_t priority_idx, uint8_t* data, size_t* len, size_t max_payload) {
    if (priority_idx >= MAX_TX_BUFFERS) return ESP_ERR_INVALID_ARG;
    if (max_payload == 0) return ESP_ERR_INVALID_SIZE;
    
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

        if (max_payload < 1) {
            vRingbufferReturnItem(slot->tx_buff, item);
            return ESP_ERR_INVALID_SIZE;
        }

        data[0] = slot->header;
        size_t payload_cap = max_payload - 1;
        size_t copy_len = (item_size > payload_cap) ? payload_cap : item_size;
        if (copy_len > 0) {
            memcpy(&data[1], item, copy_len);
        }
        *len = copy_len + 1;
        
        vRingbufferReturnItem(slot->tx_buff, item);
        
        if (item_size > payload_cap) {
            ESP_LOGW(TAG, "NOSPLIT item truncated from %zu to %zu bytes (MTU/header limit)", item_size, copy_len);
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
        data[0] = slot->header;
        *len = current_offset; // Total size is data + 1 byte header
        
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}