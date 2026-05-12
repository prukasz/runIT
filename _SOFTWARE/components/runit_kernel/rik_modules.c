#include "rik_modules.h"
#include "manager_i2c.h"
#include "manager_ble.h"
#include "interface_dispatcher.h"
#include "rik_shared.h"
#include "esp_log.h" // Dodano dla ESP_LOGI

#define TAG __FILE_NAME__

/*******************************BLE CFG********************************************/
static m_ble_cfg_t rik_ble_cfg = {
    .ble_cfg = {
        .event_group = NULL, // To be assigned in start function
        .bits = {
            .bit_indication_complete = EVENT_BIT_BLE_IND_DONE,
            .bit_notify_complete = EVENT_BIT_BLE_NOTIFY_DONE,
            .bit_indication_timeout = EVENT_BIT_BLE_IND_TIMEOUT,
            .bit_connected = EVENT_BIT_BLE_CONNECTED,
            .bit_connection_failed = EVENT_BIT_BLE_CONNECTION_FAILED,
            .bit_disconnected = EVENT_BIT_BLE_DISCONNECTED,
            .bit_mtu_update = EVENT_BIT_BLE_MTU_UPDATED,
            .bit_rx_received = EVENT_BIT_BLE_RX,
            .bit_rx_failed = EVENT_BIT_BLE_RX_FAILED
        },
        .supervisor_task_handle = NULL, // To be assigned in BLE manager task/ To be assigned in BLE manager task
    },
    .m_ble_bits_tx_start = EVENT_BIT_BLE_START,
    .m_ble_bits_tx_done = EVENT_BIT_BLE_DONE,
    .task_priority = 4,
    .task_stack_size = 8192

};
/*******************************BLE CFG********************************************/

status_rep_t rik_start_ble(EventGroupHandle_t connection_events, TaskHandle_t supervisor_task_handle) {
    rik_ble_cfg.ble_cfg.event_group = connection_events;
    rik_ble_cfg.ble_cfg.supervisor_task_handle = supervisor_task_handle;
    m_ble_buff_register_rx(rik_buff_rx);
    m_ble_buff_register_tx(rik_buff_tx, RINGBUF_TYPE_NOSPLIT, false, 0, 0, 0); 
    m_ble_buff_register_tx(rik_buff_status, RINGBUF_TYPE_BYTEBUF, true, 0xEE, sizeof(status_rep_t), 1); 

    esp_err_t err = m_ble_init(&rik_ble_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE manager");
        return STA_C(err, OWNER_RIK_DRIVER_INIT_BLE, 0);
    }
    ESP_LOGI(TAG, "BLE manager initialized successfully");
    return STA_OK;
}

/***********************************I2C CFG********************************************/
static m_i2c_config_t rik_i2c_cfg_0 = {
    .m_i2c_events = NULL,
    .m_i2c_bits_queue_process = EVENT_BIT_I2C_PROCESS,
    .m_i2c_bits_queue_done = EVENT_BIT_I2C_DONE,
    .m_i2c_bits_queue_timeout = EVENT_BIT_I2C_TIMEOUT,
    .m_i2c_bits_emergency_stop = EVENT_BIT_I2C_EMERGENCY,
    .m_i2c_bus_num = 0,
    .task_priority = 3,
    .task_stack_size = 8192,
    .core = true,
    .queue_size_aperiodic = 20,
    .queue_size_periodic = 10,
    .bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = -1, 
        .scl_io_num = -1,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0, 
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
        }
    }
};

static m_i2c_config_t rik_i2c_cfg_1 = {
    .m_i2c_events = NULL,
    .m_i2c_bits_queue_process = EVENT_BIT_I2C_PROCESS,
    .m_i2c_bits_queue_done = EVENT_BIT_I2C_DONE,
    .m_i2c_bits_queue_timeout = EVENT_BIT_I2C_TIMEOUT,
    .m_i2c_bits_emergency_stop = EVENT_BIT_I2C_EMERGENCY,
    .m_i2c_bus_num = 1,
    .task_priority = 3,
    .task_stack_size = 8192,
    .core = true,
    .queue_size_aperiodic = 20,
    .queue_size_periodic = 10,
    .bus_cfg = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = -1,
        .scl_io_num = -1,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0, 
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
        }
    }
};
/***********************************I2C CFG********************************************/

status_rep_t rik_start_i2c(EventGroupHandle_t i2c_rik_events_0, 
    EventGroupHandle_t i2c_rik_events_1,
    gpio_num_t sda_gpio_0, gpio_num_t scl_gpio_0,
    gpio_num_t sda_gpio_1, gpio_num_t scl_gpio_1)
{ 
    rik_i2c_cfg_0.m_i2c_events = i2c_rik_events_0;
    rik_i2c_cfg_0.bus_cfg.sda_io_num = sda_gpio_0;
    rik_i2c_cfg_0.bus_cfg.scl_io_num = scl_gpio_0;

    rik_i2c_cfg_1.m_i2c_events = i2c_rik_events_1;
    rik_i2c_cfg_1.bus_cfg.sda_io_num = sda_gpio_1;
    rik_i2c_cfg_1.bus_cfg.scl_io_num = scl_gpio_1;
    
    ESP_LOGI(TAG, "Event groups for I2C assigned");
    ESP_LOGI(TAG, "I2C bus 0 - SDA GPIO: %d, SCL GPIO: %d", (int)sda_gpio_0, (int)scl_gpio_0);
    ESP_LOGI(TAG, "I2C bus 1 - SDA GPIO: %d, SCL GPIO: %d", (int)sda_gpio_1, (int)scl_gpio_1);
    STA_RET_ON_ERR(m_i2c_init(&rik_i2c_cfg_0, &rik_i2c_cfg_1));
    return STA_OK;
}

static interface_cfg_t interface_cfg = {
    .connection_events = NULL,
    .connection_bits_rx = EVENT_BIT_BLE_RX,
    .interface_bits_on_complete = EVENT_BIT_INTERFACE_CMD_COMPLETE,
    .interface_bits_on_error = EVENT_BIT_INTERFACE_CMD_ERROR,
    .interface_bits_on_stop = EVENT_BIT_INTERFACE_CMD_STOP,
    .task_priority = 4,
    .task_stack_size = 4096
};

void rik_start_interface(EventGroupHandle_t events){
    interface_cfg.connection_events = events;
    interface_init(&interface_cfg);
    interface_buff_register_rx(rik_buff_rx);
}


void rik_start_status() {
    status_manager_init(rik_buff_status);
}