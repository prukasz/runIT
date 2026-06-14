#include "dac53202.h"
#include <esp_log.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DAC53202";

#define DAC53202_TIMEOUT_MS 100

#define REG_DAC_1_VOUT_CMP_CONFIG 0x03
#define REG_DAC_0_VOUT_CMP_CONFIG 0x15
#define REG_DAC_1_DATA            0x19
#define REG_DAC_0_DATA            0x1C
#define REG_COMMON_CONFIG         0x1F

#define CHECK_HANDLE_R(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)
#define RETURN_ON_ERROR(x) do {        \
    esp_err_t __err_rc = (x);          \
    if (__err_rc != ESP_OK) return __err_rc; \
} while (0)

static esp_err_t dac_write_reg(dac53202_handle_t handle, uint8_t reg, uint16_t data) {
    if (!handle || !handle->i2c_dev_handle) return ESP_ERR_INVALID_ARG;
    
    uint8_t buf[3] = { reg, (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };
    return i2c_master_transmit(handle->i2c_dev_handle, buf, sizeof(buf), DAC53202_TIMEOUT_MS);
}

static esp_err_t _dac_update_channels(dac53202_handle_t handle) {
    CHECK_HANDLE_R(handle);
    esp_err_t err = ESP_OK;
    
    if (handle->to_update.update_channels & 0x01) {
        uint16_t aligned_data = (handle->channel_raw_value[0] & 0x0FFF) << 4;
        err = dac_write_reg(handle, REG_DAC_0_DATA, aligned_data);
        if (err != ESP_OK) return err;
    }
    
    if (handle->to_update.update_channels & 0x02) {
        uint16_t aligned_data = (handle->channel_raw_value[1] & 0x0FFF) << 4;
        err = dac_write_reg(handle, REG_DAC_1_DATA, aligned_data);
        if (err != ESP_OK) return err;
    }
    
    return ESP_OK;
}

static esp_err_t _dac_update_config(dac53202_handle_t handle) {
    CHECK_HANDLE_R(handle);
    esp_err_t err = ESP_OK;

    if (handle->to_update.update_config) {
        if (!(handle->common_config & 0x0C00)) {
            err = dac_write_reg(handle, REG_DAC_0_VOUT_CMP_CONFIG, 0x0000);
            if (err != ESP_OK) return err;
        }
        if (!(handle->common_config & 0x0006)) {
            err = dac_write_reg(handle, REG_DAC_1_VOUT_CMP_CONFIG, 0x0000);
            if (err != ESP_OK) return err;
        }
        err = dac_write_reg(handle, REG_COMMON_CONFIG, handle->common_config);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

void dac53202_task(void *arg) {
    dac53202_handle_t handle = (dac53202_handle_t)arg;

    while (1) {
        uint32_t notification_value = 0;
        xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY);

        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
        TaskHandle_t caller_task = (TaskHandle_t)(uintptr_t)notification_value;
        #pragma GCC diagnostic pop

        bool error_occurred = false;

        if (handle->to_update.update_config) {
            if (_dac_update_config(handle) != ESP_OK) error_occurred = true;
            else handle->to_update.update_config = 0;
        }

        if (handle->to_update.update_channels && !error_occurred) {
            if (_dac_update_channels(handle) != ESP_OK) error_occurred = true;
            else handle->to_update.update_channels = 0;
        }

        if (caller_task) {
            xTaskNotify(caller_task, error_occurred ? 1 : 0, eSetBits);
        }
    }
}

dac53202_handle_t dac53202_new(uint8_t i2c_address) {
    dac53202_handle_t handle = calloc(1, sizeof(_dac53202_data_t));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate memory for DAC53202 handle");
        return NULL;
    }

    handle->i2c_device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    handle->i2c_device_config.device_address = i2c_address;
    handle->i2c_device_config.scl_speed_hz = 400000; 
    handle->common_config = 0x0FFF;

    if (xTaskCreate(dac53202_task, "dac_task", 4096, handle, 5, &handle->task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DAC53202 task");
        free(handle);
        return NULL;
    }

    return handle;
}

esp_err_t dac53202_preset_cfg(dac53202_handle_t handle, uint8_t channel_mask, uint8_t power_on_mask, bool update_now) {
    CHECK_HANDLE_R(handle);

    if (channel_mask & 0x01) {
        if (power_on_mask & 0x01) {
            handle->common_config &= ~(0x0C00); // Turn on VOUT0 (Clear bits [11:10])
        } else {
            handle->common_config |= (0x0C00);  // Set Hi-Z state
        }
    }
    
    if (channel_mask & 0x02) {
        if (power_on_mask & 0x02) {
            handle->common_config &= ~(0x0006); // Turn on VOUT1 (Clear bits [2:1])
        } else {
            handle->common_config |= (0x0006);  // Set Hi-Z state
        }
    }

    handle->to_update.update_config = 1;

    if (update_now) {
        esp_err_t err = _dac_update_config(handle);
        if (err == ESP_OK) handle->to_update.update_config = 0;
        return err;
    }

    return ESP_OK;
}

esp_err_t dac53202_set_voltage_raw(dac53202_handle_t handle, uint8_t channel_mask, uint16_t raw_value, bool update_now) {
    CHECK_HANDLE_R(handle);
    
    if (channel_mask & 0x01) handle->channel_raw_value[0] = raw_value;
    if (channel_mask & 0x02) handle->channel_raw_value[1] = raw_value;
    
    handle->to_update.update_channels |= (channel_mask & 0x03);

    if (update_now) {
        esp_err_t err = _dac_update_channels(handle);
        if (err == ESP_OK) handle->to_update.update_channels = 0;
        return err;
    }
    
    return ESP_OK;
}

esp_err_t dac53202_set_voltage_mv(dac53202_handle_t handle, uint8_t channel_mask, uint16_t voltage_mv, bool update_now) {
    if (voltage_mv > DAC53202_VREF_MV) {
        voltage_mv = DAC53202_VREF_MV;
    }
    
    uint16_t raw_value = (uint16_t)(((uint32_t)voltage_mv * 4095) / DAC53202_VREF_MV);
    return dac53202_set_voltage_raw(handle, channel_mask, raw_value, update_now);
}

esp_err_t dac53202_get_voltage_mv(dac53202_handle_t handle, uint8_t channel, uint16_t *voltage_mv) {
    CHECK_HANDLE_R(handle);
    if (!voltage_mv || channel > 1) return ESP_ERR_INVALID_ARG;
    
    uint16_t raw_value = handle->channel_raw_value[channel];
    *voltage_mv = (uint16_t)(((uint32_t)raw_value * DAC53202_VREF_MV) / 4095);
    
    return ESP_OK;
}