#include "ads7128.h"
#include <esp_log.h>
#include <string.h>
#include "ads7128_mock.h"

#define TAG __FILE_NAME__

#define ADS7128_I2C_TIMEOUT 20 //ms
#define I2C_FREQ_HZ 400000  // 1000khz possible

/***************Helper Macros ***************************************/
#define RETURN_ON_ERROR(x) do {        \
    esp_err_t __err_rc = (x);          \
    if (__err_rc != ESP_OK) return __err_rc; \
} while (0)

#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)
/***************Helper Macros ***************************************/

// Forward declaration
void ads_task(void* arg);



static esp_err_t _ads_update_config(ads_handle_t handle) {
    esp_err_t ret = ESP_OK;
    uint8_t pin_cfg_val = *(uint8_t*)&handle->pin_cfg;
    uint8_t gpio_cfg_val = *(uint8_t*)&handle->gpio_cfg;
    
    ret |= ads_transmit(handle->i2c_dev_handle, (uint8_t[]){PIN_CFG_ADDRESS, pin_cfg_val}, 2, ADS7128_I2C_TIMEOUT);
    
    if(gpio_cfg_val != 0) {
        gpio_cfg_val = 0; // Force GPIO configuration to 0, as the device is designed to operate with GPIOs configured as inputs
        memset(&handle->gpio_cfg, 0, sizeof(handle->gpio_cfg));
        ESP_LOGW(TAG, "Warning: Setting GPIO configuration to non-zero value may lead to undefined behavior, as the device is designed to operate with GPIOs configured as inputs (0). Forcing gpio_cfg to 0x%02X", gpio_cfg_val);
    }
    
    ret |= ads_transmit(handle->i2c_dev_handle, (uint8_t[]){GPIO_CFG_ADDRESS, gpio_cfg_val}, 2, ADS7128_I2C_TIMEOUT);
    return ret;
}

static esp_err_t _ads_update_recent_analog_values(ads_handle_t handle) {
    // Odczyt 16 bajtów zaczynając od RECENT_CH0_LSB (0xA0) do MSB kanału 7 (0xAF)
    uint8_t buf[16] = {0};

    esp_err_t ret = ads_transmit_receive(handle->i2c_dev_handle, (uint8_t[]){OP_CODE_CONTINUOUS_REGISTER_READ, RECENT_CH0_LSB_ADDRESS}, 2, buf, 16, ADS7128_I2C_TIMEOUT);
    if (ret == ESP_OK) {
        for (int i = 0; i < 8; i++) {
            handle->recent_analog_values[i] = buf[i*2] | (buf[i*2 + 1] << 8);
        }
    }
    return ret;
}

static esp_err_t _ads_update_recent_gpi_values(ads_handle_t handle){
    uint8_t buf[1] = {0};
    esp_err_t ret = ads_transmit_receive(handle->i2c_dev_handle, (uint8_t[]){OP_CODE_SINGLE_REGISTER_READ, GPI_VALUE_ADDRESS}, 2, buf, 1, ADS7128_I2C_TIMEOUT);
    if (ret == ESP_OK) {
       memcpy(&handle->recent_gpi_values, buf, 1);
    }
    return ret;
}



ads_handle_t ads_new(uint8_t i2c_address) {
    ads_handle_t handle = calloc(1, sizeof(ads_data_t));
    if (!handle) return NULL;

    memset(handle, 0, sizeof(ads_data_t));

    handle->i2c_dev_config.device_address = i2c_address;
    handle->i2c_dev_config.scl_speed_hz = 400000;
    handle->i2c_dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;

    memset(&handle->pin_cfg, 0, sizeof(handle->pin_cfg)); // Default: all channels as analog inputs
    memset(&handle->gpio_cfg, 0, sizeof(handle->gpio_cfg)); // Default: all channels as inputs

    xTaskCreate(ads_task, NULL, 4096, handle, 5, &handle->task_handle);
    return handle;
}

esp_err_t ads_set_cfg(ads_handle_t handle, uint8_t pin_cfg, uint8_t gpio_cfg, bool update_now) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    
    memcpy(&handle->pin_cfg, &pin_cfg, sizeof(uint8_t));
    memcpy(&handle->gpio_cfg, &gpio_cfg, sizeof(uint8_t));
    
    handle->to_update.cfg_to_update = 1;
    
    if (update_now) {
        handle->to_update.cfg_to_update = 0;
        _ads_update_config(handle);
    }
    return ESP_OK;
}

void ads_task(void* arg) {
    ads_handle_t handle = (ads_handle_t)arg;
    
    while(1) {
        uint32_t notification_value = 0;
        // xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY);
        if(notification_value == 0) 
        {
            _ads_update_recent_analog_values(handle);
            _ads_update_recent_gpi_values(handle);

            uint8_t gpi_val = *(uint8_t*)&handle->recent_gpi_values;
            ESP_LOGI(TAG, "Updated recent analog values: CH0=%d, CH1=%d, CH2=%d, CH3=%d, CH4=%d, CH5=%d, CH6=%d, CH7=%d",
                handle->recent_analog_values[0], handle->recent_analog_values[1], handle->recent_analog_values[2], handle->recent_analog_values[3],
                handle->recent_analog_values[4], handle->recent_analog_values[5], handle->recent_analog_values[6], handle->recent_analog_values[7]);

            ESP_LOGI(TAG, "Updated recent GPI values: 0x%02X", gpi_val);
        }
        else
        {
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
            TaskHandle_t caller_task = (TaskHandle_t)(uintptr_t)notification_value;
            #pragma GCC diagnostic pop

            if (handle->to_update.cfg_to_update) {
                _ads_update_config(handle);
                handle->to_update.cfg_to_update = 0;
            }
            xTaskNotifyGive(caller_task);
    }
    vTaskDelay(pdMS_TO_TICKS(500)); // Delay to prevent tight loop if no notifications are received
}
}
