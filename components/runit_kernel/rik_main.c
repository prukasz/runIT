#include "rik_main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <freertos/ringbuf.h>
#include "freertos/ringbuf.h"
#include "rik_modules.h"
#include "rik_onboard_drivers.h"
#include "status.h"
#include "rik_shared.h"
#include "rik_interrupts.h"


#define TAG __FILE_NAME__

static EventGroupHandle_t rik_events;

#define TX_BUFFER_SIZE 2560
#define RX_BUFFER_SIZE 2560
#define STATUS_BUFFER_SIZE 2560


RingbufHandle_t rik_buff_tx;
RingbufHandle_t rik_buff_rx;
RingbufHandle_t rik_buff_status;

/*packing tester*/
void status_gen_status_err(void* params){
    while (1) {
        tca_mock_set_pin_level(IO_TCA_INA3221_CRIT, false);
        tca_mock_set_pin_level(IO_TCA_INA3221_WARN, true);
        vTaskDelay(pdMS_TO_TICKS(100));
        tca_mock_set_pin_level(IO_TCA_INA3221_WARN, false);
       
        tca_mock_set_pin_level(IO_TCA_INA3221_CRIT, true);

        STA_PUSH(STA_E(0xDEAD, 0, 0));
        STA_PUSH(STA_E(0xDEAD, 0, 0));
        STA_PUSH(STA_E(0xDEAD, 0, 0));
        STA_PUSH(STA_E(0xDEAD, 0, 0));
        STA_PUSH(STA_E(0xDEAD, 0, 0));
        STA_PUSH(STA_E(0xDEAD, 0, 0));
        STA_PUSH(STA_E(0xDEAD, 0, 0));
        STA_PUSH(STA_E(0xDEAD, 0, 0));
        
        
        ESP_LOGI(TAG, "Generated error with code 0xDEAD and tca interrupts");
        vTaskDelay(pdMS_TO_TICKS(5000)); 
        // Generate an error every 5 seconds
        
        xEventGroupSetBits(rik_events, EVENT_BIT_BLE_START);
    }
}

esp_err_t rik_start() {
    rik_buff_tx = xRingbufferCreate(TX_BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);
    if (!rik_buff_tx) return ESP_ERR_NO_MEM;
    rik_buff_rx = xRingbufferCreate(RX_BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);
    if (!rik_buff_rx) return ESP_ERR_NO_MEM;
    rik_buff_status = xRingbufferCreate(STATUS_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (!rik_buff_status) return ESP_ERR_NO_MEM;
    

    rik_events = xEventGroupCreate();
    if (!rik_events) return ESP_ERR_NO_MEM;

    esp_err_t err = rik_start_ble(rik_events);
    if (err != ESP_OK) return err;

    rik_start_status();

    EventGroupHandle_t rik_i2c_events_0 = xEventGroupCreate();
    if (!rik_i2c_events_0) return ESP_ERR_NO_MEM;
    EventGroupHandle_t rik_i2c_events_1 = xEventGroupCreate();
    if (!rik_i2c_events_1) return ESP_ERR_NO_MEM;

    err = rik_start_i2c(rik_i2c_events_0, rik_i2c_events_1, IO_SYS_PIN_INT_I2C_SDA, IO_SYS_PIN_INT_I2C_SCL, IO_SYS_PIN_USR_I2C_SDA, IO_SYS_PIN_USR_I2C_SCL);
    if (err != ESP_OK) return err;

    rik_start_interface(rik_events);
    if (err != ESP_OK) return err;
    status_rep_t tca_res = rik_i2c_start_tca6424a(0x22, 0);
    status_rep_t ina_res = rik_i2c_start_ina3221(0x40, 0);
    if (STA_IS_ERR(tca_res)) ESP_LOGI(TAG, "Failed to start TCA6424A: e_code=%d, severity=%d", tca_res.e_code, tca_res.details.severity);
    rik_init_intr_esp();
    rik_init_intr_tca6424a();

    xTaskCreate(status_gen_status_err, "status_gen_status_err", 4096, NULL, 5, NULL);
    return ESP_OK;
    
    
}