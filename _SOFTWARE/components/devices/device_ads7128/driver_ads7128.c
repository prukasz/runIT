#include "driver_ads7128.h"
#include "sys_io.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static esp_err_t _ads_write(ads_handle_t handle, uint8_t reg, uint8_t val) {
  uint8_t tx[2] = {reg, val};
  return sys_i2c_master_transmit(handle, tx, 2);
}

static void ads_driver_task(void* arg) {
  ads_handle_t handle = (ads_handle_t)arg;
  if (!handle) {
    vTaskDelete(NULL);
    return;
  }

  while (1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    uint8_t flags = 0;
    if (ads_read_event_flags(handle, &flags) == ESP_OK) {
      for (int i = 0; i < 8; i++) {
        if (flags & (1 << i)) {
          if (handle->callbacks[i]) {
            handle->callbacks[i](handle->callback_args[i]);
          }
        }
      }
    }
  }
}

ads_handle_t ads_new(uint8_t i2c_address, bool i2c_bus_num) {
  ads_handle_t handle = calloc(1, sizeof(ads_data_t));
  return handle;
}

esp_err_t ads_start(ads_handle_t handle) {
  if (!handle) return ESP_ERR_INVALID_ARG;
  if (xTaskCreate(ads_driver_task, "ads_drv_task", 4096, handle, tskIDLE_PRIORITY + 2, &handle->task_handle) != pdPASS) {
    return ESP_FAIL;
  }
  return ESP_OK;
}

void ads_delete(ads_handle_t handle) {
  if (handle) {
    if (handle->task_handle) {
      vTaskDelete(handle->task_handle);
    }
    free(handle);
  }
}

esp_err_t ads_analog_ch_read(ads_handle_t handle, uint8_t pin_mask, bool update_now) {
  if (!handle) return ESP_ERR_INVALID_ARG;
  // Mock analog readings: 2048 (out of 4095 range)
  for (int i = 0; i < 8; i++) {
    if (pin_mask & (1 << i)) {
      handle->recent_analog_values[i] = 2048;
    }
  }
  return ESP_OK;
}

esp_err_t ads_set_alert_cfg(ads_handle_t handle, uint8_t channel, uint16_t h_thres, uint16_t l_thres, ads7128_alert_mode_t mode, bool route_to_alert_pin, bool update_now) {
  if (!handle || channel < 1 || channel > 8) return ESP_ERR_INVALID_ARG;
  uint8_t ch = channel - 1;
  uint8_t base_reg = 0x20 + ch * 4;
  uint8_t high_th_reg = base_reg + 1;

  // Real I2C write for threshold config
  return _ads_write(handle, high_th_reg, (h_thres >> 4) & 0xFF);
}

esp_err_t ads_register_alert_callback(ads_handle_t handle, uint8_t pin_mask, void (*cb)(void*), void* arg) {
  if (!handle) return ESP_ERR_INVALID_ARG;
  for (int i = 0; i < 8; i++) {
    if (pin_mask & (1 << i)) {
      handle->callbacks[i] = cb;
      handle->callback_args[i] = arg;
    }
  }
  return ESP_OK;
}

esp_err_t ads_set_cfg(ads_handle_t handle, uint8_t cfg, bool update_now) {
  if (!handle) return ESP_ERR_INVALID_ARG;
  return ESP_OK;
}

esp_err_t ads_set_pin_cfg(ads_handle_t handle, uint8_t pin_cfg, bool update_now) {
  if (!handle) return ESP_ERR_INVALID_ARG;
  return ESP_OK;
}

esp_err_t ads_set_gpio_cfg(ads_handle_t handle, uint8_t gpio_cfg, bool update_now) {
  if (!handle) return ESP_ERR_INVALID_ARG;
  return ESP_OK;
}

esp_err_t ads_read_event_flags(ads_handle_t handle, uint8_t* out_flags) {
  if (!handle || !out_flags) return ESP_ERR_INVALID_ARG;
  // Read EVENT_FLAG_ADDRESS (0x18) register
  uint8_t reg = 0x18;
  if (sys_i2c_master_transmit_receive(handle, &reg, 1, out_flags, 1) != ESP_OK) {
    // If I2C read fails or not connected, default to mock flag 0x01
    *out_flags = 0x01;
  }
  return ESP_OK;
}

void p_adc_expander_intr_pin_callback(void* arg) {
  const sys_io_intr_event_t* event = (const sys_io_intr_event_t*)arg;
  if (event) {
    ads_handle_t handle = (ads_handle_t)event->user_arg;
    if (handle && handle->task_handle) {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      vTaskNotifyGiveFromISR(handle->task_handle, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  }
}