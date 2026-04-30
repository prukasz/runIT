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
#include "manager_i2c.h" // Added I2C Manager

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
#define EVENT_BIT_BLE_DISCONNECTED      (1 << 8)
#define EVENT_BIT_BLE_MTU_UPDATED       (1 << 9)

static EventGroupHandle_t s_events_set = NULL;
static EventGroupHandle_t s_events_wait = NULL;
static RingbufHandle_t s_tx_buffer = NULL;
static RingbufHandle_t s_rx_buffer = NULL;
static uint32_t s_send_counter = 0;

/* --- I2C Definitions --- */
#define EVENT_BIT_I2C_PROCESS   (1 << 0)
#define EVENT_BIT_I2C_DONE      (1 << 1)
#define EVENT_BIT_I2C_TIMEOUT   (1 << 2)
#define EVENT_BIT_I2C_ESTOP     (1 << 3)

static EventGroupHandle_t s_i2c_events = NULL;
static uint8_t s_driver2_id = 0; // Stored to enqueue aperiodically

/* ==================================================================== */
/*                              BLE TASKS                               */
/* ==================================================================== */

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
    m_ble_buff_register_tx(s_tx_buffer, 0);

    // --- I2C INIT ---
    s_i2c_events = xEventGroupCreate();

    m_i2c_config_t bus0_cfg = {
        .m_i2c_events = s_i2c_events,
        .m_i2c_bits_queue_process = EVENT_BIT_I2C_PROCESS,
        .m_i2c_bits_queue_done = EVENT_BIT_I2C_DONE,
        .m_i2c_bits_queue_timeout = EVENT_BIT_I2C_TIMEOUT,
        .m_i2c_bits_emergency_stop = EVENT_BIT_I2C_ESTOP,
        .bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = GPIO_NUM_4, // Update these to your actual pins
            .scl_io_num = GPIO_NUM_5,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        },
        .m_i2c_bus_num = 0,
        .task_priority = 6,
        .task_stack_size = 4096,
        .core = 1,
        .queue_size_aperiodic = 10,
        .queue_size_periodic = 10
    };

    // Duplicate config for bus 1 (Adjust pins if testing both)
    m_i2c_config_t bus1_cfg = bus0_cfg;
    bus1_cfg.bus_cfg.i2c_port = I2C_NUM_1;
    bus1_cfg.bus_cfg.sda_io_num = GPIO_NUM_18; 
    bus1_cfg.bus_cfg.scl_io_num = GPIO_NUM_19;
    bus1_cfg.m_i2c_bus_num = 1;

    m_i2c_init(&bus0_cfg, &bus1_cfg);

    // --- Add I2C Drivers to Bus 0 ---
    i2c_device_config_t dev1_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x40, // Dummy addr
        .scl_speed_hz = 100000,
    };
    
    i2c_device_config_t dev2_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x41, // Dummy addr
        .scl_speed_hz = 100000,
    };

    uint8_t drv1_id = 0;
    
    // Add periodic driver (Driver 1)
    m_i2c_add_driver(0, dev1_cfg, dummy_driver1_task, true, &drv1_id);
    
    // Add aperiodic driver (Driver 2)
    m_i2c_add_driver(0, dev2_cfg, dummy_driver2_task, false, &s_driver2_id);

    // Create the task to trigger I2C bus processing
    xTaskCreate(&i2c_trigger_task, "i2c_trigger", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "=== Setup Complete! Waiting for BLE... ===");
        
    vTaskDelay(pdMS_TO_TICKS(500));
    xEventGroupWaitBits(s_events_set, EVENT_BIT_BLE_CONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);
    
    xTaskCreate(&rx_display_task, "rx_display", 4096, NULL, 5, NULL);
    xTaskCreate(&tx_periodic_task, "tx_periodic", 4096, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "=== BLE Connected, Data Exchange Started ===");
    
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}