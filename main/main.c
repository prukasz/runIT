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
#include "interface_dispatcher.h"

static const char *TAG = "MAIN";

/* --- BLE Definitions --- */
#define TX_BUFFER_SIZE 25600
#define RX_BUFFER_SIZE 25600

#define EVENT_BIT_BLE_START             (1 << 0)
#define EVENT_BIT_BLE_DONE              (1 << 1)
#define EVENT_BIT_BLE_RX                (1 << 2)
#define EVENT_BIT_BLE_NOTIFY_DONE       (1 << 3) 
#define EVENT_BIT_BLE_IND_DONE          (1 << 4) 
#define EVENT_BIT_BLE_IND_TIMEOUT       (1 << 5) 
#define EVENT_BIT_BLE_CONNECTED         (1 << 6)
#define EVENT_BIT_BLE_CONNECTION_FAILED (1 << 7)

#define EVENT_BIT_INTERFACE_CMD_COMPLETE   (1 << 8)
#define EVENT_BIT_INTERFACE_CMD_ERROR      (1 << 9)
#define EVENT_BIT_INTERFACE_CMD_STOP       (1 << 10)

static EventGroupHandle_t s_events_set = NULL;
static EventGroupHandle_t s_events_wait = NULL;
static RingbufHandle_t s_tx_buffer = NULL;
static RingbufHandle_t s_rx_buffer = NULL;
static uint32_t s_send_counter = 0;

/**
 * @brief Task to receive and display BLE messages
 */
// static void rx_display_task(void *pvParameters) {
//     (void)pvParameters;
//     uint8_t data[256];
    
//     ESP_LOGI(TAG, "RX Display task started");
    
//     while (1) {
//         xEventGroupWaitBits(s_events_set, EVENT_BIT_BLE_RX, pdTRUE, pdFALSE, portMAX_DELAY);
        
//         size_t len = sizeof(data);
//         esp_err_t ret = m_ble_rx_dequeue(data, &len);
//         if (ret == ESP_OK && len > 0) {
//             ESP_LOGI(TAG, "RX Message (%u bytes): ", (unsigned)len);
//             printf("ASCII: ");
//             for (size_t i = 0; i < len; i++) {
//                 if (data[i] >= 32 && data[i] < 127) printf("%c", data[i]);
//                 else printf(".");
//             }
//             printf("\n");
//         }
//     }
// }

/**
 * @brief Task to send periodic test messages (Speed Test Mode)
 */
static void tx_periodic_task(void *pvParameters) {
    (void)pvParameters;
    uint8_t test_data[256];
    
    ESP_LOGI(TAG, "TX Periodic task started");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); 
        snprintf((char*)test_data, sizeof(test_data), "TEST_%lu_Ping_BLE", s_send_counter++);

        for(uint8_t i = 0 ; i< 6; i++){
            esp_err_t ret = m_ble_tx_enqueue(s_tx_buffer, test_data, random() % 250+1, true);
            if(ret != ESP_OK) ESP_LOGW(TAG, "TX Buffer Full!");
        }
        xEventGroupSetBits(s_events_set, EVENT_BIT_BLE_START);
    }
}

/* ==================================================================== */
/*                              I2C TASKS                               */
/* ==================================================================== */

// Driver 1: Periodic Task
static void dummy_driver1_task(void *pvParameters) {
    while(1) {
        // Wait to be notified by the I2C Manager
        uint32_t manager_val = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        TaskHandle_t manager_task = (TaskHandle_t)manager_val;
        
        ESP_LOGI(TAG, "driver 1 periodic");
        
        // Notify manager back that transaction is completed
        if(manager_task != NULL) {
            xTaskNotifyGive(manager_task);
        }
    }
}

// Driver 2: Aperiodic Task
static void dummy_driver2_task(void *pvParameters) {
    while(1) {
        // Wait to be notified by the I2C Manager
        uint32_t manager_val = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        TaskHandle_t manager_task = (TaskHandle_t)manager_val;
        
        ESP_LOGI(TAG, "driver 2 aperiodic");
        
        // Notify manager back that transaction is completed
        if(manager_task != NULL) {
            xTaskNotifyGive(manager_task);
        }
    }
}

// Task that triggers the manager processing and periodically queues Driver 2
static void i2c_trigger_task(void *pvParameters) {
    uint32_t counter = 0;
    while(1) {
        // 1. Tick the manager every 100ms so it processes the periodic queue
        vTaskDelay(pdMS_TO_TICKS(100)); 
        xEventGroupSetBits(s_i2c_events, EVENT_BIT_I2C_PROCESS);

        counter++;
        
        // 2. Every 20 ticks (2 seconds), enqueue the aperiodic driver 2
        if (counter % 20 == 0) {
            ESP_LOGI(TAG, "--- Enqueuing Driver 2 Aperiodic Job ---");
            m_i2c_enqueue_aperiodic_job(s_driver2_id);
        }
    }
}

/* ==================================================================== */
/*                              MAIN APP                                */
/* ==================================================================== */

void app_main(void) {
    ESP_LOGI(TAG, "=== Initializing Systems ===");
    
    // --- BLE INIT ---
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
    m_ble_cfg.m_ble_bits_on_disconnect = EVENT_BIT_BLE_DISCONNECTED;
    m_ble_cfg.m_ble_bits_on_mtu_update = EVENT_BIT_BLE_MTU_UPDATED;
    m_ble_cfg.task_priority = 5;
    m_ble_cfg.task_stack_size = 4096;

    m_ble_init(&m_ble_cfg);
    m_ble_buff_register_rx(s_rx_buffer);
    m_ble_buff_register_tx(s_tx_buffer, 0);    // Priority 0: Normal TX Data

    ESP_LOGI(TAG, "BLE manager initialized");

    interface_cfg_t interface_cfg = {0};
    interface_cfg.connection_events = s_events_set;
    interface_cfg.connection_bits_rx = EVENT_BIT_BLE_RX;
    interface_cfg.interface_bits_on_complete = EVENT_BIT_INTERFACE_CMD_COMPLETE;
    interface_cfg.interface_bits_on_error = EVENT_BIT_INTERFACE_CMD_ERROR;
    interface_cfg.interface_bits_on_stop = EVENT_BIT_INTERFACE_CMD_STOP;
    interface_cfg.task_priority = 4;
    interface_cfg.task_stack_size = 4096;
    interface_init(&interface_cfg);

    interface_buff_register_rx(s_rx_buffer);
        
    vTaskDelay(pdMS_TO_TICKS(500));
    xEventGroupWaitBits(s_events_set, EVENT_BIT_BLE_CONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);
    // xTaskCreate(&rx_display_task, "rx_display", 4096, NULL, 5, NULL);
    // xTaskCreate(&tx_periodic_task, "tx_periodic", 4096, NULL, 4, NULL);
    
    
    ESP_LOGI(TAG, "=== BLE Connected, Data Exchange Started ===");
    
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}