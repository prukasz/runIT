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
#include "tps55289_mock.h"

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

static EventGroupHandle_t s_events_set = NULL;
static EventGroupHandle_t s_events_wait = NULL;
static RingbufHandle_t s_tx_buffer = NULL;
static RingbufHandle_t s_rx_buffer = NULL;
static uint32_t s_send_counter = 0;

static uint8_t s_driver1_id = 0;
static uint8_t s_driver2_id = 0;

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
        vTaskDelay(pdMS_TO_TICKS(1000)); 
        xEventGroupSetBits(s_events_set, EVENT_BIT_I2C_PROCESS);

        counter++;

        // 2. Every 20 ticks (2 seconds), enqueue the aperiodic driver 2        
        if (counter % 20 == 0) {
            ESP_LOGI(TAG, "--- Enqueuing Driver 2 Aperiodic Job ---");
            m_i2c_enqueue_aperiodic_job(s_driver2_id);
        }
    }
}

static void test_tca6424_mock_task(void *pvParameters) {
    ESP_LOGI(TAG, "Odpalanie testowego taska dla TCA6424A Mock.");
    
    // Test: konfigureujemy cześć pinów jako wyjścia
    uint8_t write_buf[] = {0x8C, 0x0F, 0xF0, 0xFF}; // AutoIncrement=1, Reg=Config_P0
    tca_transmit(NULL, write_buf, sizeof(write_buf), 100);

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Sprawdzamy czy symulator wystawił INT
        if (tca_get_int_pin_level() == 0) {
            ESP_LOGI(TAG, ">>> ZGLOSZENIE PRZERWANIA INT! Odczytywanie rejestrow...");
            
            // Odczyt rejestrów wejściowych (wyczyści przerwanie)
            uint8_t write_req[] = {0x80}; // AutoIncrement=1, Reg=Input_P0
            uint8_t read_buf[3];
            tca_transmit_receive(NULL, write_req, sizeof(write_req), read_buf, sizeof(read_buf), 100);
            
            ESP_LOGI(TAG, ">>> Przeczytano wejscia: P0=0x%02X, P1=0x%02X, P2=0x%02X. INT pin teraz = %d",
                     read_buf[0], read_buf[1], read_buf[2], tca_get_int_pin_level());
        }
    }
}

static void test_tps55289_mock_task(void *pvParameters) {
    ESP_LOGI(TAG, "Odpalanie testowego taska dla TPS55289 Mock.");
    
    // Włączmy napięcie wyjściowe (OE) oraz maskę przerwań OVP (zgodnie ze specyfikacją dokumentacji - bity: SCP (7), OCP (6), OVP (5))
    uint8_t write_buf1[] = {0x06, 0xA0}; // Adres 0x06 (MODE), wartosc: 0xA0 (OE=1, HICCUP=1)
    tps_transmit(NULL, write_buf1, sizeof(write_buf1), 100);
    
    uint8_t write_buf2[] = {0x05, 0xE0}; // Adres 0x05 (INT_MASK), wartosc: 0xE0 (OVP, OCP, SCP odmaskowane)
    tps_transmit(NULL, write_buf2, sizeof(write_buf2), 100);

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Sprawdzamy stan pinu FB/INT (Aktywny na LOW po OVP)
        if (tps_get_int_pin_level() == 0) {
            ESP_LOGI(TAG, ">>> ZGLOSZENIE OVP NA TPS! Odczyt rejestru STATUS...");
            
            uint8_t write_req[] = {0x07}; // STATUS (0x07)
            uint8_t read_buf[1];
            tps_transmit_receive(NULL, write_req, sizeof(write_req), read_buf, sizeof(read_buf), 100);
            
            ESP_LOGI(TAG, ">>> STATUS: 0x%02X (INT pin po wyczyszczeniu flag zalezy od stanu pinu)", read_buf[0]);
        }
    }
}

/* ==================================================================== */
/*                              MAIN APP                                */
/* ==================================================================== */

#if 0
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

    // --- I2C Manager Init ---
    m_i2c_config_t bus0_config = {
        .m_i2c_events = s_events_set,
        .m_i2c_bits_queue_process = EVENT_BIT_I2C_PROCESS,
        .m_i2c_bits_queue_done = EVENT_BIT_I2C_DONE,
        .m_i2c_bits_queue_timeout = EVENT_BIT_I2C_TIMEOUT,
        .m_i2c_bits_emergency_stop = 0,
        .m_i2c_bus_num = 0,
        .task_priority = 10,
        .task_stack_size = 4096,
        .core = true,
        .queue_size_aperiodic = 10,
        .queue_size_periodic = 10,
        .bus_cfg = {
            .i2c_port =  I2C_NUM_0,
            .sda_io_num = 18,
            .scl_io_num = 19,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = true,
            }
        }
    };

    m_i2c_config_t bus1_config = bus0_config; 
    bus1_config.m_i2c_bus_num = I2C_NUM_1;
    bus1_config.bus_cfg.i2c_port = I2C_NUM_1;
    bus1_config.bus_cfg.sda_io_num = 4;
    bus1_config.bus_cfg.scl_io_num = 5;

    m_i2c_init(&bus0_config, &bus1_config);

    // Add drivers
    i2c_device_config_t dummy_dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x40, // Dummy Address
        .scl_speed_hz = 100000,
    };
    
    m_i2c_add_driver(0, dummy_dev_config, dummy_driver1_task, true, &s_driver1_id);

    i2c_device_config_t dummy_dev2_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x41, // Dummy Address
        .scl_speed_hz = 100000,
    };
    
    m_i2c_add_driver(0, dummy_dev2_config, dummy_driver2_task, false, &s_driver2_id);

    // --- TCA6424A Mock Init ---
    init_tca_mock();
    xTaskCreate(test_tca6424_mock_task, "tca_mock_test", 2048, NULL, 5, NULL);

    // --- TPS55289 Mock Init ---
    init_tps_mock();
    xTaskCreate(test_tps55289_mock_task, "tps_mock_test", 2048, NULL, 5, NULL);

    // Start testing
    xTaskCreate(i2c_trigger_task, "i2c_trigger", 2048, NULL, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(500));
    xEventGroupWaitBits(s_events_set, EVENT_BIT_BLE_CONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);
    //xTaskCreate(&rx_display_task, "rx_display", 4096, NULL, 5, NULL);
    xTaskCreate(&tx_periodic_task, "tx_periodic", 4096, NULL, 4, NULL);
    
    
    ESP_LOGI(TAG, "=== BLE Connected, Data Exchange Started ===");
    
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}
#endif

// --- TPS55289 Test Task ---
static void test_tps55289_task(void *pvParameters) {
    ESP_LOGI(TAG, "Odpalanie testowego taska dla TPS55289 Mock.");
    
    // 1. Ustawienie MODE - OE=1 (bit 7), HICCUP=1 (bit 5) na dresie 0x06
    uint8_t write_buf1[] = {0x06, 0xA0}; 
    tps_transmit(NULL, write_buf1, sizeof(write_buf1), 100);
    
    // 2. Odczyt maski prze_rwan przed zmiana
    uint8_t write_req_mask[] = {0x05};
    uint8_t read_mask[1] = {0};
    tps_transmit_receive(NULL, write_req_mask, sizeof(write_req_mask), read_mask, sizeof(read_mask), 100);
    ESP_LOGI(TAG, "Initial INT MASK read: 0x%02X", read_mask[0]);

    // 3. Odmaskowanie podstawowych bledow (OVP, OCP, SCP) na dresie 0x05
    uint8_t write_buf2[] = {0x05, 0xE0}; 
    tps_transmit(NULL, write_buf2, sizeof(write_buf2), 100);
    ESP_LOGI(TAG, "Odmaskowano (0xE0) OVP, OCP, SCP na 0x05.");

    uint8_t test_vout_val = 0x10;

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // --- TEST BASIC I2C R/W ---
        // Zapis testowy do rejestru VOUT_LSB (0x00) i IOUT_LIMIT (0x02)
        uint8_t test_write[] = {0x00, test_vout_val}; // Rejestr 0x00, wartosc testowa
        tps_transmit(NULL, test_write, sizeof(test_write), 100);
        
        // Odczyt z rejestru by sprawdzic czy mock trzyma nasze dane (transmit_receive)
        uint8_t test_req[] = {0x00};
        uint8_t test_read[1] = {0};
        tps_transmit_receive(NULL, test_req, sizeof(test_req), test_read, sizeof(test_read), 100);
        
        ESP_LOGI(TAG, "I2C Check -> Zapisano: 0x%02X, Odczytano z 0x00: 0x%02X", test_vout_val, test_read[0]);
        test_vout_val++; // Zmieniamy wartosc by upewnic sie ze sie aktualizuje

        // --- TEST INTERRUPT (OVP, OCP, SCP) ---
        // Polling pinu sprzetowego
        if (tps_get_int_pin_level() == 0) {
            ESP_LOGW(TAG, ">>> Przerwanie aktywne na TPS55289 (LOW) <<<");
            
            // Odczyt status by wyczyscic interrupt
            uint8_t write_req_status[] = {0x07};
            uint8_t read_status[1] = {0};
            tps_transmit_receive(NULL, write_req_status, sizeof(write_req_status), read_status, sizeof(read_status), 100);
            
            ESP_LOGI(TAG, "TPS55289 STATUS odczytany: 0x%02X", read_status[0]);
            
            if (read_status[0] & 0x80) {
                ESP_LOGE(TAG, "Tps Fault: Short Circuit Proteciton (SCP).");
            }
            if (read_status[0] & 0x40) {
                ESP_LOGE(TAG, "Tps Fault: Over Current (OCP).");
            }
            if (read_status[0] & 0x20) {
                ESP_LOGW(TAG, "Tps Warning: Over Voltage (OVP).");
            }
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== Initializing Test Environment TPS55289 ===");

    // Inicjalizacja mocka
    init_tps_mock();

    // Utworzenie zewnetrznego taska mikrokontrolera do obsługi odczytu
    xTaskCreate(test_tps55289_task, "test_tps_mock", 3072, NULL, 5, NULL);

    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}