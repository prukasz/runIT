#include "rik_main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/ringbuf.h"
#include "rik_modules.h"
#include "rik_devices.h"
#include "status.h"
#include "rik_shared.h"
#include "manager_ble.h"
#include "rtos_utils.h"
#include "rik_scheduler.h"  
#include "rik_logs.h"
#include "rik_tx_rx.h"
#include "rik_status_handler.h"
#include "rik_devices_link.h"
#include "manager_io.h"
#include "manager_power.h"
#include "esp_timer.h"
#include "rik_system_ctrl.h"
#include "vm_demo.h"

#define TAG "RIK_MAIN"

#define TX_BUFFER_SIZE     2560
#define RX_BUFFER_SIZE     2560
#define STATUS_BUFFER_SIZE 2560
#define LOG_BUFFER_SIZE    2560

/***********************STATIC GLOBAL BUFFERS ***********************************/
R_RINGBUFFER_DEFINE(rik_buff_tx,         TX_BUFFER_SIZE,     RINGBUF_TYPE_NOSPLIT);
R_RINGBUFFER_DEFINE(rik_buff_rx,         RX_BUFFER_SIZE,     RINGBUF_TYPE_NOSPLIT);
R_RINGBUFFER_DEFINE(rik_buff_status,     STATUS_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
R_RINGBUFFER_DEFINE(rik_buff_esp_log,    LOG_BUFFER_SIZE,    RINGBUF_TYPE_NOSPLIT);
/***********************STATIC GLOBAL BUFFERS ***********************************/

/***********************STATIC GLOBAL EVENT GROUPS ******************************/
R_EVENT_GROUP_DEFINE(rik_events_wireless);

bool _rik_ble_active;
bool _rik_wifi_active;

R_EVENT_GROUP_DEFINE(rik_events_wired);
R_EVENT_GROUP_DEFINE(rik_events_data_processing);
R_EVENT_GROUP_DEFINE(rik_events_vm);
/***********************STATIC GLOBAL EVENT GROUPS ******************************/


esp_err_t rik_start(void) {
    status_rep_t rep;
    rik_scheduler_start();
    rik_status_handler_start(rik_buff_status, rik_scheduler_get_task_handle());
    rep = rik_start_ble(rik_events_wireless, rik_scheduler_get_task_handle());
    if (!STA_IS_OK(rep)) {
        STA_P(rep);
        ESP_LOGE(TAG, "Failed to start BLE module");
        return ESP_FAIL;
    }
    
    rep = rik_start_i2c(
        rik_scheduler_get_task_handle(),
        rik_events_wired,
        rik_events_wired, 
        SYS_IO_GET_PIN(RIK_IO_PIN_INTERNAL_I2C_SDA),
        SYS_IO_GET_PIN(RIK_IO_PIN_INTERNAL_I2C_SCL), 
        SYS_IO_GET_PIN(RIK_IO_PIN_USR_I2C_SDA),
        SYS_IO_GET_PIN(RIK_IO_PIN_USR_I2C_SCL)
    );
    if (!STA_IS_OK(rep)) {
        STA_P(rep);
        ESP_LOGE(TAG, "Failed to start I2C module");
        return ESP_FAIL;
    }

    rep = rik_start_io_manager();
    if (!STA_IS_OK(rep)) {
        STA_P(rep);
        ESP_LOGE(TAG, "Failed to start IO manager");
        return ESP_FAIL;
    }
    rep = rik_start_power_manager();
    if (!STA_IS_OK(rep)) {
        STA_P(rep);
        ESP_LOGE(TAG, "Failed to start power manager");
        return ESP_FAIL;
    }

    rik_start_interface(rik_events_wireless);

    rep = rik_link_pins();
    if (!STA_IS_OK(rep)) {
        STA_P(rep);
        ESP_LOGE(TAG, "Failed to link devices");
        return ESP_FAIL;
    }
    rep = rik_link_interrupts();
    if (!STA_IS_OK(rep)) {
        STA_P(rep);
        ESP_LOGE(TAG, "Failed to link interrupts");
        return ESP_FAIL;
    }

    vTaskDelay(MSEC(1000)); 
    vm_demo_start();
    //sys_devices_default_config();
    return ESP_OK;
}