#include "rik_main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/ringbuf.h"
#include "rik_modules.h"
#include "rik_onboard_drivers.h"
#include "status.h"
#include "rik_shared.h"
#include "rik_interrupts.h"
#include "tps55289_mock.h"
#include "manager_ble.h"
#include "rtos_utils.h"
#include "rik_scheduler.h"  
#include "rik_logs.h"

#define TAG "RIK_MAIN"

#define TX_BUFFER_SIZE     2560
#define RX_BUFFER_SIZE     2560
#define STATUS_BUFFER_SIZE 2560
#define LOG_BUFFER_SIZE    2560


// Ring Buffers
R_RINGBUFFER_DEFINE(rik_buff_tx,     TX_BUFFER_SIZE,     RINGBUF_TYPE_NOSPLIT);
R_RINGBUFFER_DEFINE(rik_buff_rx,     RX_BUFFER_SIZE,     RINGBUF_TYPE_NOSPLIT);
R_RINGBUFFER_DEFINE(rik_buff_status, STATUS_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
R_RINGBUFFER_DEFINE(rik_buff_log,    LOG_BUFFER_SIZE,    RINGBUF_TYPE_NOSPLIT);

// Event Groups
R_EVENT_GROUP_DEFINE(rik_events_communication);
R_EVENT_GROUP_DEFINE(rik_events_processing);
R_EVENT_GROUP_DEFINE(rik_i2c_events_0);
R_EVENT_GROUP_DEFINE(rik_i2c_events_1);

/* =====================================================================
 * 2. TASKS & FUNCTIONS
 * ===================================================================== */

/* packing tester */
void status_gen_status_err(void* params){
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
       
        tps_trigger_ocp(0x74);
        tps_trigger_scp(0x75);

        // Assuming STA_P is a macro that pushes to a queue/buffer
        STA_P(STA_E(0xDEAD, 0, 0));
        STA_P(STA_E(0xDEAD, 0, 0));
        STA_P(STA_E(0xDEAD, 0, 0));
        STA_P(STA_E(0xDEAD, 0, 0));
        STA_P(STA_E(0xDEAD, 0, 0));
        STA_P(STA_E(0xDEAD, 0, 0));
        STA_P(STA_E(0xDEAD, 0, 0));
        STA_P(STA_E(0xDEAD, 0, 0));
        
        ESP_LOGW(TAG, "Generated error with code 0xDEAD and tca interrupts");
        vTaskDelay(pdMS_TO_TICKS(5000)); 
        
        xEventGroupSetBits(rik_events_communication, EVENT_BIT_BLE_TX_START);
    }
}
bool _rik_ble_active;
bool _rik_wifi_active;
esp_err_t rik_start(void) {
    status_rep_t rep;

    // 1. Start BLE
    rik_scheduler_start(); // Start the scheduler before initializing BLE to ensure it's ready for task creation
    rep = rik_start_ble(rik_events_communication, rik_scheduler_get_task_handle());
    if (!STA_IS_OK(rep)) return rep.e_code;

    // 2. Start Status Manager
    rik_start_status();

    // 3. Start I2C Drivers
    rep = rik_start_i2c(rik_i2c_events_0, rik_i2c_events_1, 
                        IO_SYS_PIN_INT_I2C_SDA, IO_SYS_PIN_INT_I2C_SCL, 
                        IO_SYS_PIN_USR_I2C_SDA, IO_SYS_PIN_USR_I2C_SCL);
    if (!STA_IS_OK(rep)) return rep.e_code;

    // 4. Start Interface
    rik_start_interface(rik_events_processing);

#ifdef CONFIG_CONNECT_TCA6424A
    status_rep_t tca_res = rik_i2c_start_tca6424a(0x22, 0);
    if (!STA_IS_OK(tca_res)) {
        ESP_LOGI(TAG, "Failed to start TCA6424A: e_code=%d, severity=%d", 
                 tca_res.e_code, tca_res.details.severity);
    }
    rik_init_pins_callbacks_tca6424a();
#endif
    
#ifdef CONFIG_CONNECT_INA3221
    status_rep_t ina_res = rik_i2c_start_ina3221(0x40, 0);
    if (!STA_IS_OK(ina_res)) {
        ESP_LOGI(TAG, "Failed to start INA3221: e_code=%d, severity=%d", 
                 ina_res.e_code, ina_res.details.severity);
    }
#endif

#ifdef CONFIG_CONNECT_TPS55289
    status_rep_t tps_res = rik_i2c_start_tsp55289(0x74, 0x75, 0);
    if (!STA_IS_OK(tps_res)) {
        ESP_LOGI(TAG, "Failed to start TPS55289: e_code=%d, severity=%d", 
                 tps_res.e_code, tps_res.details.severity);
    }
    rik_init_usr_callbacks_tps55289();
#endif

    // 5. System Interrupts & Tester Task
    rik_init_intr_esp();
    xTaskCreate(status_gen_status_err, "status_gen_status_err", 4096, NULL, 5, NULL);
    
    R_EVENT_AWAIT_ANY(rik_events_communication, EVENT_BIT_BLE_CONNECTED, WAIT_FOREVER);
    rik_start_interface(rik_events_communication); // Initialize BLE-related interrupts after BLE is connected to avoid spurious events during startup
    esp_log_set_vprintf(rik_log_vprintf);
    _rik_ble_active = true;
    rik_log_remote_enable(true);
    esp_log_level_set("*", ESP_LOG_INFO);
    
    return ESP_OK;
}