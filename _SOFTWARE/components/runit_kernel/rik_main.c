#include "rik_main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/ringbuf.h"
#include "rik_modules.h"
#include "rik_devices.h"
#include "status.h"
#include "rik_shared.h"
#include "rik_interrupts.h"
#include "manager_ble.h"
#include "rtos_utils.h"
#include "rik_scheduler.h"  
#include "rik_logs.h"
#include "rik_tx_rx.h"
#include "rik_status_handler.h"
#include "esp_adc_config.h"

#define TAG "RIK_MAIN"

#define TX_BUFFER_SIZE     2560
#define RX_BUFFER_SIZE     2560
#define STATUS_BUFFER_SIZE 2560
#define LOG_BUFFER_SIZE    2560

/***********************STATIC GLOBAL BUFFERS ***********************************/
R_RINGBUFFER_DEFINE(rik_buff_tx,     TX_BUFFER_SIZE,     RINGBUF_TYPE_NOSPLIT);
R_RINGBUFFER_DEFINE(rik_buff_rx,     RX_BUFFER_SIZE,     RINGBUF_TYPE_NOSPLIT);
R_RINGBUFFER_DEFINE(rik_buff_status, STATUS_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
R_RINGBUFFER_DEFINE(rik_buff_log,    LOG_BUFFER_SIZE,    RINGBUF_TYPE_NOSPLIT);
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

    // 1. Start BLE
    rik_status_handler_start(rik_buff_status, rik_scheduler_get_task_handle());
    rik_scheduler_start(); // Start the scheduler before initializing BLE to ensure it's ready for task creation
    rep = rik_start_ble(rik_events_wireless, rik_scheduler_get_task_handle());
    if (!STA_IS_OK(rep)) return rep.e_code;

    xEventGroupWaitBits(rik_events_wireless, EVENT_BIT_BLE_CONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);
    // 3. Start I2C Drivers
    rep = rik_start_i2c(rik_scheduler_get_task_handle(), rik_events_wired, rik_events_wired, 
                        IO_SYS_PIN_INT_I2C_SDA, IO_SYS_PIN_INT_I2C_SCL, 
                        IO_SYS_PIN_USR_I2C_SDA, IO_SYS_PIN_USR_I2C_SCL);
    if (!STA_IS_OK(rep)) return rep.e_code;


    #ifdef CONFIG_CONNECT_TCA6424A
    status_rep_t tca_res = rik_gpio_expander_start(0x22, 0);
        if (!STA_IS_OK(tca_res)) {
            ESP_LOGI(TAG, "Failed to start TCA6424A: e_code=%d, severity=%d", 
                    tca_res.e_code, tca_res.details.severity);
        }
   #endif
    rik_i2c_start_adc(0x10, 0);
    
    rik_start_power_manager();
    rik_start_interface(rik_events_wireless);
    
    // 5. System Interrupts & Tester Task

    esp_adc_start();
    set_active_adc_channels(0xFF);
    rik_init_intr_esp();

    
    return ESP_OK;
}