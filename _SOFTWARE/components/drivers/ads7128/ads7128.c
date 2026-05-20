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
    return ads_transmit(handle->i2c_dev_handle, (uint8_t[]){OP_CODE_SINGLE_REGISTER_WRITE, GENERAL_CFG_ADDRESS, *(uint8_t*)&handle->config}, 3, ADS7128_I2C_TIMEOUT);
}

static esp_err_t _ads_update_gpio_config(ads_handle_t handle) {
    return ads_transmit(handle->i2c_dev_handle, (uint8_t[]){OP_CODE_SINGLE_REGISTER_WRITE, GPIO_CFG_ADDRESS, *(uint8_t*)&handle->gpio_cfg}, 3, ADS7128_I2C_TIMEOUT);
}

static esp_err_t _ads_update_pin_config(ads_handle_t handle) {
    return ads_transmit(handle->i2c_dev_handle, (uint8_t[]){OP_CODE_SINGLE_REGISTER_WRITE, PIN_CFG_ADDRESS, *(uint8_t*)&handle->pin_cfg}, 3, ADS7128_I2C_TIMEOUT);
}

static esp_err_t _ads_update_alert_config(ads_handle_t handle, uint8_t channel) {
    if (channel < 1 || channel > 8) return ESP_ERR_INVALID_ARG;

    ads7128_ch_alert_config_t* alert_cfg = &handle->alert_configs[channel-1];
    
    uint8_t buf[6];
    buf[0] = OP_CODE_CONTINUOUS_REGISTER_WRITE;
    buf[1] = HYSTERESIS_CH0_ADDRESS + (channel - 1) * 4; // Adres rejestru progu kanału (każdy kanał zajmuje 6 bajtów konfiguracji)
    memcpy(&buf[2], &alert_cfg->histeresis_config, 1);
    buf[3] = alert_cfg->h_thres_msb;
    memcpy(&buf[4], &alert_cfg->event_count_config, 1);
    buf[5] = alert_cfg->l_thres_msb;

    RETURN_ON_ERROR(ads_transmit(handle->i2c_dev_handle, buf, 6, ADS7128_I2C_TIMEOUT));

    if(alert_cfg->route_to_alert_pin){
        RETURN_ON_ERROR(ads_transmit(handle->i2c_dev_handle, (uint8_t[]){OP_CODE_SET_BIT, ALERT_CH_SEL_ADDRESS, 1 << (channel - 1)}, 3, ADS7128_I2C_TIMEOUT));
    } else {
        RETURN_ON_ERROR(ads_transmit(handle->i2c_dev_handle, (uint8_t[]){OP_CODE_CLEAR_BIT, ALERT_CH_SEL_ADDRESS, 1 << (channel - 1)}, 3, ADS7128_I2C_TIMEOUT));
    }

    return ESP_OK;
}

static esp_err_t _ads_update_ch_analog_value(ads_handle_t handle, uint8_t channel) {
    if (channel < 1 || channel > 8) return ESP_ERR_INVALID_ARG;

    // Odczyt 16 bajtów zaczynając od RECENT_CH0_LSB (0xA0) do MSB kanału 7 (0xAF)
    uint8_t buf[2] = {0};

    esp_err_t ret = ads_transmit_receive(handle->i2c_dev_handle, (uint8_t[]){OP_CODE_SINGLE_REGISTER_READ, RECENT_CH0_LSB_ADDRESS + (channel - 1) * 2}, 2, buf, 2, ADS7128_I2C_TIMEOUT);
    if (ret == ESP_OK) {
        handle->recent_analog_values[channel - 1] = buf[0] | (buf[1] << 8);
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

static esp_err_t _ads_check_alert(ads_handle_t handle, uint8_t* channel){
    uint8_t buf = 0;
    RETURN_ON_ERROR(ads_transmit_receive(handle->i2c_dev_handle, (uint8_t[]){OP_CODE_SINGLE_REGISTER_READ, EVENT_FLAG_ADDRESS}, 2, &buf, 1, ADS7128_I2C_TIMEOUT));
    *channel = __builtin_clz(buf);
    RETURN_ON_ERROR(ads_transmit(handle->i2c_dev_handle, (uint8_t[]){OP_CODE_CLEAR_BIT, EVENT_HIGH_FLAG_ADDRESS, 1<<(*channel)}, 3, ADS7128_I2C_TIMEOUT));
    RETURN_ON_ERROR(ads_transmit(handle->i2c_dev_handle, (uint8_t[]){OP_CODE_CLEAR_BIT, EVENT_LOW_FLAG_ADDRESS, 1<<(*channel)}, 3, ADS7128_I2C_TIMEOUT));

    return ESP_OK;
}

void ads_isr_callback(void* arg) {
    ads_handle_t handle = (ads_handle_t)arg;
    BaseType_t high_task_wakeup = pdFALSE;
    
    // Flag that a hardware interrupt occurred
    handle->alert_triggered = true;

    xTaskNotifyFromISR(handle->task_handle, 0, eNoAction, &high_task_wakeup);
    
    if (high_task_wakeup) {
        portYIELD_FROM_ISR();
    }
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
    memset(&handle->config, 16, sizeof(handle->config));

    xTaskCreate(ads_task, NULL, 4096, handle, 5, &handle->task_handle);
    return handle;
}

esp_err_t ads_set_cfg(ads_handle_t handle, uint8_t cfg, bool update_now) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    
    memcpy(&handle->config, &cfg, sizeof(uint8_t));
    
    handle->to_update.config_to_update = 1;
    
    if (update_now) {
        handle->to_update.config_to_update = 0;
        _ads_update_config(handle);
    }
    return ESP_OK;
}

esp_err_t ads_set_gpio_cfg(ads_handle_t handle, uint8_t gpio_cfg, bool update_now) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    
    memcpy(&handle->gpio_cfg, &gpio_cfg, sizeof(uint8_t));
    
    handle->to_update.gpio_cfg_to_update = 1;
    
    if (update_now) {
        handle->to_update.gpio_cfg_to_update = 0;
        _ads_update_gpio_config(handle);
    }
    return ESP_OK;
}


esp_err_t ads_set_pin_cfg(ads_handle_t handle, uint8_t pin_cfg, bool update_now) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    
    memcpy(&handle->pin_cfg, &pin_cfg, sizeof(uint8_t));
    
    handle->to_update.pin_cfg_to_update = 1;
    
    if (update_now) {
        handle->to_update.pin_cfg_to_update = 0;
        _ads_update_pin_config(handle);
    }
    return ESP_OK;
}


esp_err_t ads_set_alert_cfg(ads_handle_t handle, uint8_t channel, uint16_t h_thres, uint16_t l_thres, ads7128_alert_mode_t mode, bool route_to_alert_pin, bool update_now){
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (channel-1 > 7) return ESP_ERR_INVALID_ARG;
    
    ads7128_ch_alert_config_t* alert_cfg = &handle->alert_configs[channel-1];
    memcpy(&alert_cfg->histeresis_config, &h_thres, 1);
    memcpy(&alert_cfg->h_thres_msb, ((uint8_t*)&h_thres) + 1, 1);
    memcpy(&alert_cfg->event_count_config, &l_thres, 1);
    memcpy(&alert_cfg->l_thres_msb, ((uint8_t*)&l_thres) + 1, 1);
    alert_cfg->mode = mode;
    alert_cfg->route_to_alert_pin = route_to_alert_pin;

    handle->to_update.alert_config_to_update = 1;
    
    if (update_now) {
        handle->to_update.alert_config_to_update = 0;
        _ads_update_alert_config(handle, channel);
    }
    return ESP_OK;
}

esp_err_t ads_register_alert_callback(ads_handle_t handle, uint8_t pin_mask, void (*cb)(void*), void* arg)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    uint8_t pin = __builtin_ctz(pin_mask); // Get index of least significant set bit
    if (pin > 7) return ESP_ERR_INVALID_ARG; // Ensure it's within 0-7

    handle->callbacks[pin] = cb;
    handle->callback_args[pin] = arg;

    return ESP_OK;
}


void ads_task(void* arg) {
    ads_handle_t handle = (ads_handle_t)arg;
    
    while(1) {
        uint32_t notification_value = 0;
        xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY);

        if(handle->alert_triggered)
        {
            handle->alert_triggered = false;
            uint8_t channel;

            _ads_check_alert(handle, &channel);

            if(handle->callbacks[channel])
            {
                handle->callbacks[channel](handle->callback_args[channel]);
            }
        }
        
        if(notification_value == 0) 
        {
            for (int i = 1; i <= 8; i++) {
                _ads_update_ch_analog_value(handle, i);
            }
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

            if (handle->to_update.config_to_update) {
                _ads_update_config(handle);
                handle->to_update.config_to_update = 0;
            }

            if(handle->to_update.gpio_cfg_to_update) {
                _ads_update_gpio_config(handle);
                handle->to_update.gpio_cfg_to_update = 0;
            }

            if(handle->to_update.pin_cfg_to_update) {
                _ads_update_pin_config(handle);
                handle->to_update.pin_cfg_to_update = 0;
            }

            if(handle->to_update.alert_config_to_update) {
                for(uint8_t ch = 1; ch <= 8; ch++) {
                    _ads_update_alert_config(handle, ch);
                }
                handle->to_update.alert_config_to_update = 0;
            }
            xTaskNotifyGive(caller_task);
        }
}
}
