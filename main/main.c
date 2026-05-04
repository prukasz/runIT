#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include <esp_random.h>


#include "manager_i2c.h"
#include "manager_ble.h"
#include "a_ble.h"
#include "esp_timer.h"
#include "interface_dispatcher.h"
#include "tca6424a_mock.h"
#include "tca6424a.h"

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

#define EVENT_BIT_INTERFACE_CMD_COMPLETE   (1 << 8)
#define EVENT_BIT_INTERFACE_CMD_ERROR      (1 << 9)
#define EVENT_BIT_INTERFACE_CMD_STOP       (1 << 10)

#define EVENT_BIT_I2C_PROCESS (1 << 11)
#define EVENT_BIT_I2C_DONE    (1 << 12)
#define EVENT_BIT_I2C_TIMEOUT (1 << 13)

static EventGroupHandle_t events = NULL;

static RingbufHandle_t s_tx_buffer = NULL;
static RingbufHandle_t s_rx_buffer = NULL;
static uint32_t s_send_counter = 0;

static void rx_display_task(void *pvParameters) {
    (void)pvParameters;
    uint8_t data[256];
    
    ESP_LOGI(TAG, "RX Display task started");
    
    while (1) {
        xEventGroupWaitBits(events, EVENT_BIT_BLE_RX, pdTRUE, pdFALSE, portMAX_DELAY);
        
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
        xEventGroupSetBits(events, EVENT_BIT_BLE_START);
    }
}


// Task that triggers the manager processing and periodically queues Driver 2
static void i2c_trigger_task(void *pvParameters) {
    uint32_t counter = 0;
    while(1) {

        vTaskDelay(pdMS_TO_TICKS(500)); 
        xEventGroupSetBits(events, EVENT_BIT_I2C_PROCESS);
        counter++;
    }
}

// 1. Declare our test callback for the TCA interrupt
static void test_pin_interrupt(void* arg) {
    int pin_num = (int)(intptr_t)arg;
    ESP_LOGW(TAG, "=> SUCCESS: Callback invoked for TCA pin %d!", pin_num);
}

static void test_tca6424_mock_task(void *pvParameters) {
    ESP_LOGI(TAG, "Odpalanie testowego taska sterujacego Mock.");
    
    tca_handle_t tca_dev_handle = (tca_handle_t)pvParameters;

    // Configure Port 1 and Port 2 as Inputs (0xFF), Port 0 as Outputs (0x00)
    tca_preset_cfg(tca_dev_handle, 0xFFFFFF, 0xFFFF00, true); // 0xFFFFFF mask, 0xFFFF00 state

    // Test output updates on Port 0
    tca_preset_pins(tca_dev_handle, 0x000000FF, 0x000000A5, true); 

    // Register callbacks
    tca_register_pin_callback(tca_dev_handle, 23, test_pin_interrupt, TCA_ON_FALLING_EDGE, (void*)23);
    tca_register_pin_callback(tca_dev_handle, 22, test_pin_interrupt, TCA_ON_CHANGE, (void*)22);

    bool pin_level = true;
    bool toggle = true;
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        pin_level = !pin_level;

        // Toggle mock pins
        tca_mock_set_pin_level(23, pin_level);
        tca_mock_set_pin_level(22, pin_level);
        
        tca_preset_pins(tca_dev_handle, 0x000000FF, toggle ? 0x000000A5 : 0x0000005A, false);
        toggle = !toggle;
        // Simulating the ISR trigger check inside the TCA task loop naturally
        if (tca_get_int_pin_level() == 0) {
            ESP_LOGI(TAG, ">>> ZGLOSZENIE PRZERWANIA INT z symulatora!");
            tca_isr_callback((void*)tca_dev_handle);
        }
    }
}

/* ==================================================================== */
/*                              MAIN APP                                */
/* ==================================================================== */

void app_main(void) {

    events = xEventGroupCreate();


    /***********************************ble manager config**************************************/
    s_tx_buffer = xRingbufferCreate(TX_BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);
    s_rx_buffer = xRingbufferCreate(RX_BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);

    m_ble_cfg_t m_ble_cfg = {0};
    m_ble_cfg.m_ble_events = events;
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
    /*****************************ble manager config**************************************/


    /*********************** interface config **************************************/

    interface_cfg_t interface_cfg = {0};
    interface_cfg.connection_events = events;
    interface_cfg.connection_bits_rx = EVENT_BIT_BLE_RX;
    interface_cfg.interface_bits_on_complete = EVENT_BIT_INTERFACE_CMD_COMPLETE;
    interface_cfg.interface_bits_on_error = EVENT_BIT_INTERFACE_CMD_ERROR;
    interface_cfg.interface_bits_on_stop = EVENT_BIT_INTERFACE_CMD_STOP;
    interface_cfg.task_priority = 4;
    interface_cfg.task_stack_size = 4096;
    interface_init(&interface_cfg);

    interface_buff_register_rx(s_rx_buffer);

    /*********************** interface config **************************************/

    /************************** I2C config only bus 0 ********************************** */
    m_i2c_config_t* bus0_config = calloc(1, sizeof(m_i2c_config_t));
    bus0_config->m_i2c_events = events;
    bus0_config->m_i2c_bits_queue_process = EVENT_BIT_I2C_PROCESS;
    bus0_config->m_i2c_bits_queue_done = EVENT_BIT_I2C_DONE;
    bus0_config->m_i2c_bits_queue_timeout = EVENT_BIT_I2C_TIMEOUT;
    bus0_config->m_i2c_bits_emergency_stop = 0;
    bus0_config->m_i2c_bus_num = 0;
    bus0_config->task_priority = 10;
    bus0_config->task_stack_size = 4096;
    bus0_config->core = true;
    bus0_config->queue_size_aperiodic = 10;
    bus0_config->queue_size_periodic = 10;
    bus0_config->bus_cfg.i2c_port = I2C_NUM_0;
    bus0_config->bus_cfg.sda_io_num = 18;
    bus0_config->bus_cfg.scl_io_num = 19;
    bus0_config->bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus0_config->bus_cfg.glitch_ignore_cnt = 7;
    bus0_config->bus_cfg.intr_priority = 0;
    bus0_config->bus_cfg.trans_queue_depth = 0;
    bus0_config->bus_cfg.flags.enable_internal_pullup = true;

    m_i2c_config_t* bus1_config = calloc(1, sizeof(m_i2c_config_t));
    *bus1_config = *bus0_config; // Copy all config from bus0
    bus1_config->m_i2c_bus_num = 1;
    bus1_config->bus_cfg.i2c_port = I2C_NUM_1;
    bus1_config->bus_cfg.sda_io_num = 21;
    bus1_config->bus_cfg.scl_io_num = 20;
    

    m_i2c_init(bus0_config, bus1_config);

    xTaskCreate(i2c_trigger_task, "i2c_trigger", 4096, NULL, 5, NULL);

    /************************** I2C config only bus 0 ********************************** */


    /*************************TCA Config********************************** */

    tca_handle_t tca_dev_handle = tca_new(20, GPIO_NUM_8); // I2C address 0x20, interrupt pin GPIO4
   
    status_err_report_t rep = m_i2c_add_driver(0, tca_dev_handle->i2c_dev_config, tca_dev_handle->task_handle, true, NULL);
    if (STA_IS_OK(rep))
        {
            ESP_LOGI(TAG, "TCA6424A driver added successfully to I2C Manager");
        }
    xTaskCreate(test_tca6424_mock_task, "tca_mock_test", 4096, tca_dev_handle, 5, NULL);

    /*************************TCA Config********************************** */
   


    vTaskDelay(pdMS_TO_TICKS(500));
    xEventGroupWaitBits(events, EVENT_BIT_BLE_CONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);
    //xTaskCreate(&rx_display_task, "rx_display", 4096, NULL, 5, NULL);
    xTaskCreate(&tx_periodic_task, "tx_periodic", 4096, NULL, 4, NULL);
    
    
    ESP_LOGI(TAG, "=== BLE Connected, Data Exchange Started ===");
    
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}