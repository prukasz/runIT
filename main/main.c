#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include <esp_random.h>

#include "manager_ble.h"
#include "a_ble.h"
#include "esp_timer.h"

static const char *TAG = "MAIN";

/* Ring buffer sizes */
#define TX_BUFFER_SIZE 25600
#define RX_BUFFER_SIZE 25600

/* Distinct event group bits for synchronization */
#define EVENT_BIT_BLE_START            (1 << 0) // Wakes up the BLE task to start sending
#define EVENT_BIT_BLE_DONE             (1 << 1) // Set by BLE task when all TX queues are empty
#define EVENT_BIT_BLE_RX               (1 << 2) // Ble RX received

#define EVENT_BIT_BLE_NOTIFY_DONE      (1 << 3) 
#define EVENT_BIT_BLE_IND_DONE         (1 << 4) 
#define EVENT_BIT_BLE_IND_TIMEOUT      (1 << 5) 
#define EVENT_BIT_BLE_CONNECTED        (1 << 6)
#define EVENT_BIT_BLE_CONNECTION_FAILED (1 << 7)

static EventGroupHandle_t s_events_set = NULL;
static EventGroupHandle_t s_events_wait = NULL;
static RingbufHandle_t s_tx_buffer = NULL;
static RingbufHandle_t s_rx_buffer = NULL;

static uint32_t s_send_counter = 0;

/**
 * @brief Task to receive and display BLE messages
 */
static void rx_display_task(void *pvParameters) {
    (void)pvParameters;
    uint8_t data[256];
    
    ESP_LOGI(TAG, "RX Display task started");
    
    while (1) {
        xEventGroupWaitBits(s_events_set, EVENT_BIT_BLE_RX, pdTRUE, pdFALSE, portMAX_DELAY);
        
        size_t len = sizeof(data);
        esp_err_t ret = m_ble_rx_dequeue(data, &len);
        if (ret == ESP_OK && len > 0) {
            ESP_LOGI(TAG, "RX Message (%u bytes): ", (unsigned)len);
            printf("ASCII: ");
            for (size_t i = 0; i < len; i++) {
                if (data[i] >= 32 && data[i] < 127) printf("%c", data[i]);
                else printf(".");
            }
            printf("\n");
        }
    }
}

/**
 * @brief Task to send periodic test messages (Speed Test Mode)
 */
static void tx_periodic_task(void *pvParameters) {
    (void)pvParameters;
    uint8_t test_data[256];
    
    ESP_LOGI(TAG, "TX Periodic task started");
    
    while (1) {
        // Speed testing: 10ms delay
        vTaskDelay(pdMS_TO_TICKS(60)); 



        // 2. Format Data
        snprintf((char*)test_data, sizeof(test_data), "TEST_%lu_Ping_BLE", s_send_counter++);

        
        // 3. Enqueue Data

        for(uint8_t i = 0 ; i< 6; i++){
        esp_err_t ret = m_ble_tx_enqueue(s_tx_buffer, test_data, random() % 250+1);
        if(ret != ESP_OK) ESP_LOGW(TAG, "TX Buffer Full!");
        }
    
        xEventGroupSetBits(s_events_set, EVENT_BIT_BLE_START);

    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== BLE Manager Test ===");
    
    s_events_set = xEventGroupCreate();
    s_events_wait = xEventGroupCreate();
    
    s_tx_buffer = xRingbufferCreate(TX_BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);
    s_rx_buffer = xRingbufferCreate(RX_BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);

    m_ble_cfg_t m_ble_cfg = {0};
    m_ble_cfg.m_ble_events = s_events_set;
    m_ble_cfg.m_ble_bits_tx_start = EVENT_BIT_BLE_START;
    m_ble_cfg.m_ble_bits_tx_done = EVENT_BIT_BLE_DONE;
    m_ble_cfg.m_ble_bits_on_rx = EVENT_BIT_BLE_RX;

    m_ble_cfg.m_ble_bits_on_notify_complete = EVENT_BIT_BLE_NOTIFY_DONE;
    m_ble_cfg.m_ble_bits_on_indication_complete = EVENT_BIT_BLE_IND_DONE;
    m_ble_cfg.m_ble_bits_on_indication_timeout = EVENT_BIT_BLE_IND_TIMEOUT;
    m_ble_cfg.m_ble_bits_on_connect = EVENT_BIT_BLE_CONNECTED;
    m_ble_cfg.m_ble_bits_on_connection_failed = EVENT_BIT_BLE_CONNECTION_FAILED;
    
    m_ble_cfg.task_priority = 5;
    m_ble_cfg.task_stack_size = 4096;

    m_ble_init(&m_ble_cfg);

    m_ble_buff_register_rx(s_rx_buffer);
    m_ble_buff_register_tx(s_tx_buffer, 0);    // Priority 0: Normal TX Data

    ESP_LOGI(TAG, "BLE manager initialized");
        
    vTaskDelay(pdMS_TO_TICKS(500));
    xEventGroupWaitBits(s_events_set, EVENT_BIT_BLE_CONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);
    xTaskCreate(&rx_display_task, "rx_display", 4096, NULL, 5, NULL);
    xTaskCreate(&tx_periodic_task, "tx_periodic", 4096, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "=== Test started, waiting for BLE connections ===");
    
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}