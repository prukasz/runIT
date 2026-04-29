#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <string.h>
#include <stdio.h>

#include "manager_ble.h"
#include "a_ble.h"
#include "status.h"

static const char *TAG = "MAIN";

/* Ring buffer sizes */
#define TX_BUFFER_SIZE 2560
#define RX_BUFFER_SIZE 2560
#define ERROR_BUFFER_SIZE 1024

/* Distinct event group bits for synchronization */
#define EVENT_BIT_BLE_START  (1 << 0) // Wakes up the BLE task to start sending
#define EVENT_BIT_BLE_DONE   (1 << 1) // Set by BLE task when all TX queues are empty

static EventGroupHandle_t s_events_set = NULL;
static EventGroupHandle_t s_events_wait = NULL;
static RingbufHandle_t s_tx_buffer = NULL;
static RingbufHandle_t s_rx_buffer = NULL;
static RingbufHandle_t s_error_buffer = NULL;

static uint32_t s_send_counter = 0;

/**
 * @brief Task to receive and display BLE messages
 */
static void rx_display_task(void *pvParameters) {
    (void)pvParameters;
    uint8_t data[256];
    
    ESP_LOGI(TAG, "RX Display task started");
    
    while (1) {
        size_t len = sizeof(data);
        esp_err_t ret = m_ble_rx_dequeue(data, &len);
        if (ret == ESP_OK && len > 0) {
            ESP_LOGI(TAG, "RX Message (%u bytes): ", (unsigned)len);
            for (size_t i = 0; i < len; i++) {
                printf("%02x ", data[i]);
            }
            printf("\nASCII: ");
            for (size_t i = 0; i < len; i++) {
                if (data[i] >= 32 && data[i] < 127) printf("%c", data[i]);
                else printf(".");
            }
            printf("\n");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/**
 * @brief Task to send periodic test messages
 */
static void tx_periodic_task(void *pvParameters) {
    (void)pvParameters;
    uint8_t test_data[64];
    
    ESP_LOGI(TAG, "TX Periodic task started");
    
    while (1) {
        snprintf((char*)test_data, sizeof(test_data), "TEST_%lu_Hello_BLE", s_send_counter++);
        size_t len = strlen((const char*)test_data);
        
        // Pass the actual buffer handle (s_tx_buffer) directly to enqueue
        esp_err_t ret = m_ble_tx_enqueue(s_tx_buffer, test_data, len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "TX Message failed with code: %d", ret);
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief Task to produce test error messages
 */
static void error_producer_task(void *pvParameters) {
    (void)pvParameters;
    uint32_t err_counter = 0;
    
    ESP_LOGI(TAG, "Error Producer task started");
    
    while (1) {
        status_err_report_t err = {
            .code = err_counter,
            .details.severity = 2, // CRITICAL
        };
        err.origin_name = 1; 
        
        // Assuming your existing macro pushes directly to the ringbuffer
        STA_ERR_PUSH_TO_BUFFER_OVERWRITE(s_error_buffer, &err);
        ESP_LOGE(TAG, "Generated Test Error: %lu", err_counter);
        
        // Manually wake up the BLE task to process the new error
        xEventGroupSetBits(s_events_wait, EVENT_BIT_BLE_START);
        
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== BLE + Status Manager Test ===");
    a_ble_init();
    
    s_events_set = xEventGroupCreate();
    s_events_wait = xEventGroupCreate();
    
    s_tx_buffer = xRingbufferCreate(TX_BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);
    s_rx_buffer = xRingbufferCreate(RX_BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);
    s_error_buffer = xRingbufferCreate(ERROR_BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);

    s_manager_init(s_events_set, s_events_wait, EVENT_BIT_BLE_START, EVENT_BIT_BLE_DONE, s_error_buffer);
    
    m_ble_init(s_events_set, s_events_wait, EVENT_BIT_BLE_START, EVENT_BIT_BLE_DONE, 5);

    m_ble_buff_register_rx(s_rx_buffer);
    
    // Register the buffers once so the BLE task knows which ones to poll (and in what order)
    m_ble_buff_register_tx(s_error_buffer, 0); // Priority 0: Errors
    m_ble_buff_register_tx(s_tx_buffer, 1);    // Priority 1: Normal TX Data

    ESP_LOGI(TAG, "BLE manager initialized");

    vTaskDelay(pdMS_TO_TICKS(500));
    
    if (xTaskCreate(&rx_display_task, "rx_display", 4096, NULL, 5, NULL) != pdPASS) return;
    if (xTaskCreate(&tx_periodic_task, "tx_periodic", 4096, NULL, 4, NULL) != pdPASS) return;
    if (xTaskCreate(&error_producer_task, "error_producer", 4096, NULL, 4, NULL) != pdPASS) return;
    
    ESP_LOGI(TAG, "=== Test started, waiting for BLE connections ===");
    
    uint32_t loop_count = 0;
    while (1) {
        loop_count++;
        ESP_LOGI(TAG, "Main loop alive (count=%lu)", loop_count);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}