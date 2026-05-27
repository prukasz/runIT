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
#include "rik_devices_link.h"
#include "manager_io.h"
#include "ads7128_mock.h"
#include "manager_power.h"
#include "esp_timer.h"

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



void task_read_adc(void* arg){
    while(1){
        uint32_t adc_value[4];


        sys_io_adc_read(rik_gpio_esp_port_id,SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IPROPI_1)|SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IPROPI_2)|
        SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IPROPI_3)|SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IPROPI_4), adc_value, 4);

        STA_P(SYS_IO_SET_PWM_FREQ(RIK_PWM_EXPANDER_USER_CHANNEL_7, 50));
        vTaskDelay(pdMS_TO_TICKS(1000));
        SYS_GPIO_TOGGLE(RIK_IO_PIN_GPIO_EXPANDER_nRESET);
        STA_P(SYS_IO_SET_PWM_DUTY(RIK_PWM_EXPANDER_USER_CHANNEL_7, 200));
        vTaskDelay(pdMS_TO_TICKS(1000));
        STA_P(SYS_IO_SET_PWM_DUTY(RIK_PWM_EXPANDER_USER_CHANNEL_7, 300));
        
        uint32_t voltage = 0;
        int32_t current = 0;
        sys_pwr_get_bus_current(RIK_CHANNEL_VREG0, &current);
        sys_pwr_get_bus_voltage(RIK_CHANNEL_VREG0, &voltage);
        ESP_LOGI(TAG, "Voltage Regulator 0 Voltage: %u mV, current: %d", voltage, current);
        ESP_LOGI(TAG, "ADC Value: %u, %u, %u, %u", adc_value[0], adc_value[1], adc_value[2], adc_value[3]);

    }
}

R_TASK_DEFINE(adc_task, 4096);


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
    manager_io_unfreeze();
    R_TASK_START(adc_task, task_read_adc, NULL, 5);
    manager_io_freeze();
    manager_io_unfreeze();

    R_EVENT_SET(rik_events_wired, EVENT_BIT_I2C_PROCESS_0); // Trigger data processing after initial setup
    R_EVENT_AWAIT_ALL(rik_events_wired, EVENT_BIT_I2C_DONE_0, WAIT_FOREVER);
    
    return ESP_OK;
}