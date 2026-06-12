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
#undef OWNER
#define OWNER OWNER_RIK_DRIVER_INIT_BLE
status_rep_t rik_start_ble(EventGroupHandle_t connection_events, TaskHandle_t supervisor_task_handle) {
    rik_ble_cfg.event_group = connection_events;
    rik_ble_cfg.supervisor_task_handle = supervisor_task_handle;
    m_ble_buff_register_rx(rik_buff_rx);
    m_ble_buff_register_tx(rik_buff_tx, RINGBUF_TYPE_NOSPLIT, false, 0, 0, 0); 
    m_ble_buff_register_tx(rik_buff_status, RINGBUF_TYPE_BYTEBUF, true, 0xEE, sizeof(status_rep_t), 1);
    m_ble_buff_register_tx(rik_buff_esp_log, RINGBUF_TYPE_NOSPLIT, false, 0, 0, 2); 

    CHECK_ESP_CALL_RP(m_ble_init(&rik_ble_cfg));
    
    ESP_LOGI(TAG, "BLE manager initialized successfully");
    return STA_OK;
}
/*******************************BLE CFG********************************************/

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
    STA_RP_ON_ERR(m_i2c_init(&rik_i2c_cfg_0, &rik_i2c_cfg_1));
    return STA_OK;
}

// /*******************INTERFACE CONFIG******************************** */


void rik_start_interface(EventGroupHandle_t events){
    interface_init();
    interface_buff_register_rx(rik_buff_rx);
}


/****************POWER MANAGER CONFIG******************************** */
#undef OWNER
#define OWNER OWNER_RIK_DRIVER_INIT_POWER_MANAGER
status_rep_t rik_start_power_manager() {
    #if CONFIG_CONNECT_TPS55289_0 || CONFIG_CONNECT_TPS55289_1
        STA_RP_ON_ERR(rik_p_vreg_start(CONFIG_I2C_ADDR_TPS55289_0, CONFIG_I2C_ADDR_TPS55289_1, 0));
    #endif
    #if CONFIG_CONNECT_INA3221
        STA_RP_ON_ERR(rik_current_monitor_start(CONFIG_I2C_ADDR_INA3221, 0));
    #endif
    #if CONFIG_CONNECT_AP33772S
        STA_RP_ON_ERR(rik_p_power_delivery_start(CONFIG_I2C_ADDR_AP33772S, 0));
    #endif

    STA_RP_ON_ERR(manager_pwr_init());
    ESP_LOGI(TAG, "Power manager initialized successfully");
    return STA_OK;
}
/****************POWER MANAGER CONFIG******************************** */


/**************IO MANAGER CONFIG************************************* */
#undef OWNER
#define OWNER OWNER_RIK_DRIVER_INIT_IO_MANAGER
status_rep_t rik_start_io_manager() {
    
    STA_RP_ON_ERR(rik_p_gpio_esp_start());
#if CONFIG_CONNECT_TCA6424A
    STA_RP_ON_ERR(rik_p_gpio_expander_start(CONFIG_I2C_ADDR_TCA6424A, 0));
#endif
#ifdef CONFIG_CONNECT_ADS7128
    STA_RP_ON_ERR(rik_adc_expander_start(CONFIG_I2C_ADDR_ADS7128, 0));
#endif
#if CONFIG_CONNECT_PCA9685
    STA_RP_ON_ERR(p_pwm_expadner_start(CONFIG_I2C_ADDR_PCA9685, 0)); 
#endif
    ESP_LOGI(TAG, "IO manager initialized successfully");
    return STA_OK;
}
/**************IO MANAGER CONFIG************************************* */
