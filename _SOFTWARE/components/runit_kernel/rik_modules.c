#include "rik_modules.h"
#include "manager_i2c.h"
#include "manager_ble.h"
#include "manager_power.h"
#include "rik_devices.h"
#include "interface_dispatcher.h"
#include "config_power.h"
#include "config_io.h"
#include "rik_shared.h"
#include "esp_log.h"
#include "provider_adc_expander.h"
#include "provider_gpio_esp.h"
#include "provider_gpio_expander.h"
#include "sdkconfig.h"

#define TAG __FILE_NAME__

/*******************************BLE CFG********************************************/
static m_ble_cfg_t rik_ble_cfg = {
    .bit_on_connect = EVENT_BIT_BLE_CONNECTED,
    .bit_on_disconnect = EVENT_BIT_BLE_DISCONNECTED,
    .bit_on_connection_failed = EVENT_BIT_BLE_CONNECTION_FAILED,
    .bit_on_mtu_change = EVENT_BIT_BLE_MTU_UPDATED,
    .bit_on_rx_received = EVENT_BIT_BLE_ON_RX,
    .bit_on_rx_failed = EVENT_BIT_BLE_ON_RX_FAILED,
    .bit_tx_start = EVENT_BIT_BLE_TX_START,
    .bit_tx_done = EVENT_BIT_BLE_TX_DONE,
    .manager_task_handle = NULL,
    .supervisor_task_handle = NULL,
    .event_group = NULL
};
/*******************************BLE CFG********************************************/

status_rep_t rik_start_ble(EventGroupHandle_t connection_events, TaskHandle_t supervisor_task_handle) {
    rik_ble_cfg.event_group = connection_events;
    rik_ble_cfg.supervisor_task_handle = supervisor_task_handle;
    m_ble_buff_register_rx(rik_buff_rx);
    m_ble_buff_register_tx(rik_buff_tx, RINGBUF_TYPE_NOSPLIT, false, 0, 0, 0); 
    m_ble_buff_register_tx(rik_buff_status, RINGBUF_TYPE_BYTEBUF, true, 0xEE, sizeof(status_rep_t), 1);
    m_ble_buff_register_tx(rik_buff_esp_log, RINGBUF_TYPE_NOSPLIT, false, 0, 0, 2); 

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
    .m_i2c_bit_queue_process = EVENT_BIT_I2C_PROCESS_0,
    .m_i2c_bit_queue_done = EVENT_BIT_I2C_DONE_0,
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
    .m_i2c_bit_queue_process = EVENT_BIT_I2C_PROCESS_1,
    .m_i2c_bit_queue_done = EVENT_BIT_I2C_DONE_1,
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

status_rep_t rik_start_i2c(TaskHandle_t supervisor_task_handle, EventGroupHandle_t i2c_events_bus_0, EventGroupHandle_t i2c_events_bus_1,
    gpio_num_t sda_gpio_0, gpio_num_t scl_gpio_0,
    gpio_num_t sda_gpio_1, gpio_num_t scl_gpio_1)
{ 
    rik_i2c_cfg_0.supervisor_task_handle = supervisor_task_handle;
    rik_i2c_cfg_0.m_i2c_events = i2c_events_bus_0;
    rik_i2c_cfg_0.bus_cfg.sda_io_num = sda_gpio_0;
    rik_i2c_cfg_0.bus_cfg.scl_io_num = scl_gpio_0;

    rik_i2c_cfg_1.supervisor_task_handle = supervisor_task_handle;
    rik_i2c_cfg_1.m_i2c_events = i2c_events_bus_1;
    rik_i2c_cfg_1.bus_cfg.sda_io_num = sda_gpio_1;
    rik_i2c_cfg_1.bus_cfg.scl_io_num = scl_gpio_1;
    
    ESP_LOGI(TAG, "I2C bus 0 - SDA GPIO: %d, SCL GPIO: %d", sda_gpio_0, scl_gpio_0);
    ESP_LOGI(TAG, "I2C bus 1 - SDA GPIO: %d, SCL GPIO: %d", sda_gpio_1, scl_gpio_1);
    return m_i2c_init(&rik_i2c_cfg_0, &rik_i2c_cfg_1);
}

// /*******************INTERFACE CONFIG******************************** */


void rik_start_interface(EventGroupHandle_t events){
    /* register config parsers before starting the interface */
    cfg_pwr_init();
    cfg_io_init();
    interface_init(NULL);
    interface_buff_register_rx(rik_buff_rx);
}


// /****************POWER MANAGER CONFIG******************************** */

// /****************POWER MANAGER CONFIG******************************** */

status_rep_t rik_start_power_manager() {
    rik_regs_start(0x74, 0x75, 0);
    rik_current_monitor_start(0x40, 0);
    return manager_pwr_init();
}

status_rep_t rik_start_io_manager() {
    STA_RET_ON_ERR(rik_gpio_esp_start());
#if defined(CONFIG_CONNECT_TCA6424A) && CONFIG_CONNECT_TCA6424A
    STA_RET_ON_ERR(rik_gpio_expander_start(0x11, 0));
#endif
#ifdef CONFIG_CONNECT_ADS7218
    STA_RET_ON_ERR(rik_adc_expander_start(0x12, 0));
#endif
    
    p_pwm_expadner_start(0x60, 0); 
    return STA_OK;
    
}

