#include "rik_main.h"
#include "rik_shared.h"
#include "manager_ble.h"
#include "rtos_utils.h"
#include "rik_scheduler.h"  
#include "rik_logs.h"
#include "rik_tx_rx.h"
#include "rik_status_handler.h"
#include "rik_devices_link.h"
#include "rik_modules.h"
#include "manager_io.h"
#include "manager_power.h"
#include "vm_demo.h"
#include "rik_system_ctrl.h"

#define TAG __FILE_NAME__


/***********************STATIC GLOBAL BUFFERS ***********************************/
R_RINGBUFFER_DEFINE(rik_buff_tx,         CONFIG_BUFFER_SIZE_TX,     RINGBUF_TYPE_NOSPLIT);
R_RINGBUFFER_DEFINE(rik_buff_rx,         CONFIG_BUFFER_SIZE_RX,     RINGBUF_TYPE_NOSPLIT);
R_RINGBUFFER_DEFINE(rik_buff_status,     CONFIG_BUFFER_SIZE_STATUS_REP, RINGBUF_TYPE_BYTEBUF);
R_RINGBUFFER_DEFINE(rik_buff_esp_log,    CONFIG_BUFFER_SIZE_ESP_LOG,    RINGBUF_TYPE_NOSPLIT);
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
    rik_status_handler_start(rik_buff_status, rik_scheduler_get_task_handle());
    rik_scheduler_start();
    rik_start_ble(rik_events_wireless, rik_scheduler_get_task_handle());
    rik_start_i2c(
        rik_scheduler_get_task_handle(),
        rik_events_wired,
        rik_events_wired, 
        SYS_IO_GET_PIN(RIK_IO_PIN_INTERNAL_I2C_SDA),
        SYS_IO_GET_PIN(RIK_IO_PIN_INTERNAL_I2C_SCL), 
        SYS_IO_GET_PIN(RIK_IO_PIN_USR_I2C_SDA),
        SYS_IO_GET_PIN(RIK_IO_PIN_USR_I2C_SCL)
    );
    rik_start_io_manager();
    rik_start_power_manager();
    rik_start_interface(rik_events_wireless);
    rik_link_pins();
    rik_link_interrupts();

    vTaskDelay(MSEC(1000)); 
    //vm_demo_start();
    return ESP_OK;
}