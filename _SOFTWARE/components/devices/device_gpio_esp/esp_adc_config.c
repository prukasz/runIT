#include "esp_adc_config.h"
#include <stdint.h>
#include <stdlib.h>
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "shared_io_types.h"
#include "sys_device.h"
#include "sys_io.h"
#include "utils.h"

#define TAG __FILE_NAME__

#define RETURN_ON_ERROR(x)                   \
  do {                                       \
    esp_err_t __err_rc = (x);                \
    if (__err_rc != ESP_OK) return __err_rc; \
  } while (0)

#define CONFIG_ESP_ADC_FRAME_SIZE_BYTES 256
#define CONFIG_ESP_ADC_SAMPLE_RATE_HZ 20000
#define CONFIG_ESP_ADC_IIR_ALPHA 0.1f
#define CONFIG_PRIORITY_ESP_ADC_TASK 5

R_MUTEX_DEFINE(adc_mutex);
R_TASK_DEFINE(adc_processing_task, 4096);
R_BINARY_SEM_DEFINE(adc_sync_sem);

static adc_continuous_handle_t adc_handle = NULL;
static uint16_t last_channels_mask = 0;
static bool is_adc_running = false;

static volatile bool _needs_hardware_reconfig = false;

// --- Helper: Safely tears down and rebuilds the ADC hardware ---
static esp_err_t reconfigure_adc_hardware(uint16_t channel_mask) {
  if (adc_handle != NULL) {
    if (is_adc_running) {
      RETURN_ON_ERROR(adc_continuous_stop(adc_handle));
      is_adc_running = false;
    }
    RETURN_ON_ERROR(adc_continuous_deinit(adc_handle));
    adc_handle = NULL;
  }

  if (channel_mask == 0) return ESP_OK;

  adc_digi_pattern_config_t adc_pattern[10];
  uint8_t pattern_count = 0;

  for (uint8_t i = 0; i < 10; i++) {
    if (channel_mask & (1U << i)) {
      adc_pattern[pattern_count] = (adc_digi_pattern_config_t){
          .atten = ADC_ATTEN_DB_12,
          .channel = i,
          .unit = ADC_UNIT_1,
          .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
      };
      pattern_count++;
    }
  }

  adc_continuous_handle_cfg_t handle_cfg = {
      .max_store_buf_size = 4096,
      .conv_frame_size = CONFIG_ESP_ADC_FRAME_SIZE_BYTES,
  };
  RETURN_ON_ERROR(adc_continuous_new_handle(&handle_cfg, &adc_handle));

  adc_continuous_config_t adc_config = {
      .pattern_num = pattern_count,
      .adc_pattern = adc_pattern,
      .sample_freq_hz = CONFIG_ESP_ADC_SAMPLE_RATE_HZ,
      .conv_mode = ADC_CONV_SINGLE_UNIT_1,
      .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
  };

  RETURN_ON_ERROR(adc_continuous_config(adc_handle, &adc_config));
  RETURN_ON_ERROR(adc_continuous_start(adc_handle));
  is_adc_running = true;
  return ESP_OK;
}

// =========================================================================
// PUBLIC FUNCTIONS (Synchronous wrappers)
// =========================================================================

uint16_t compute_active_channels_mask(void) {
  uint16_t mask = 0;
  if (R_MUTEX_LOCK(gpio_mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < GPIO_NUM_MAX; i++) {
      esp_pin_obj_t* pin_obj = pin_obj_get(i);
      if (pin_obj && pin_obj->pin_mode == SYS_IO_MODE_ADC) {
        adc_unit_t unit;
        adc_channel_t chan;
        if (adc_continuous_io_to_channel(i, &unit, &chan) == ESP_OK && unit == ADC_UNIT_1) {
          mask |= (1U << chan);
        }
      }
    }
    R_MUTEX_UNLOCK(gpio_mutex);
  }
  return mask;
}

esp_err_t esp_adc_update_active_channels(void) {
  if (R_MUTEX_LOCK(adc_mutex, WAIT_FOREVER) != pdTRUE) return ESP_FAIL;
  last_channels_mask = compute_active_channels_mask();

  // Consume any leftover semaphore tokens to prevent early returns
  if (adc_sync_sem != NULL) {
    xSemaphoreTake(adc_sync_sem, 0);
  }

  _needs_hardware_reconfig = true;
  R_MUTEX_UNLOCK(adc_mutex);

  if (adc_sync_sem != NULL) {
    xSemaphoreTake(adc_sync_sem, portMAX_DELAY);
  }
  return ESP_OK;
}

// =========================================================================
// BACKGROUND HARDWARE TASK HELPERS & LOOP
// =========================================================================

static void process_adc_channel(int pin, int chan, uint32_t sum, uint16_t count, esp_pin_obj_t* pin_obj) {
  if (count == 0) return;

  float coarse_raw = (float)sum / count;
  pin_adc_data_t* adc_cfg = &pin_obj->hw.adc_cfg;
  adc_cfg->internal_raw_filtered = (CONFIG_ESP_ADC_IIR_ALPHA * coarse_raw) + ((1.0f - CONFIG_ESP_ADC_IIR_ALPHA) * adc_cfg->internal_raw_filtered);

  if (adc_cfg->cali_handle == NULL) return;

  int voltage_mv = 0;
  adc_cali_raw_to_voltage(adc_cfg->cali_handle, (int)adc_cfg->internal_raw_filtered, &voltage_mv);

  adc_cfg->adc_last_read_mv = (uint16_t)voltage_mv;

  if (!gpio_esp_ctx.base.is_frozen) {
    adc_cfg->adc_cached_mv = (uint16_t)voltage_mv;
  }

  if (pin_obj->intr_config.mode == SYS_IO_INTR_DISABLE) return;

  bool condition_met = false;
  bool reset_condition_met = false;
  uint8_t wt = pin_obj->intr_config.mode;
  uint16_t up = pin_obj->intr_config.adc.adc_threshold_up_mV;
  uint16_t down = pin_obj->intr_config.adc.adc_threshold_down_mV;
  uint16_t hyst = pin_obj->intr_config.adc.adc_threshold_hysteresis_mV;

  if (wt == SYS_IO_INTR_ADC_WINDOW_OUTSIDE || wt == SYS_IO_INTR_MODE_BOTH_EDGES) {
    condition_met = (voltage_mv >= up || voltage_mv <= down);
    reset_condition_met = (voltage_mv < (up - hyst) && voltage_mv > (down + hyst));
  } else if (wt == SYS_IO_INTR_ADC_WINDOW_INSIDE) {
    condition_met = (voltage_mv <= up && voltage_mv >= down);
    reset_condition_met = (voltage_mv > (up + hyst) || voltage_mv < (down - hyst));
  } else if (wt == SYS_IO_INTR_MODE_RISING_EDGE) {
    condition_met = (voltage_mv >= up);
    reset_condition_met = (voltage_mv < (up - hyst));
  } else if (wt == SYS_IO_INTR_MODE_FALLING_EDGE) {
    condition_met = (voltage_mv <= down);
    reset_condition_met = (voltage_mv > (down + hyst));
  }

  if (condition_met && !adc_cfg->alert_was_triggered) {
    adc_cfg->alert_was_triggered = true;
    if (pin_obj->intr_config.own_func.own_func) {
      SYS_CB_OWN(pin_obj->intr_config.own_func);
    } else {
      SYS_IO_CB(&gpio_esp_ctx, pin_obj->io_num, wt, voltage_mv, pin_obj->intr_config.route_mask, pin_obj->intr_config.action_mask);
    }
  } else if (reset_condition_met) {
    adc_cfg->alert_was_triggered = false;
  }
}

static void adc_processing_task_function(void* pvParameters) {
  (void)pvParameters;
  uint8_t raw_buffer[CONFIG_ESP_ADC_FRAME_SIZE_BYTES];
  uint32_t ret_num = 0;

  if (last_channels_mask > 0) {
    if (R_MUTEX_LOCK(adc_mutex, portMAX_DELAY) == pdTRUE) {
      reconfigure_adc_hardware(last_channels_mask);
      R_MUTEX_UNLOCK(adc_mutex);
    }
  }

  while (1) {
    if (_needs_hardware_reconfig) {
      if (R_MUTEX_LOCK(adc_mutex, portMAX_DELAY) == pdTRUE) {
        ESP_LOGI(TAG, "Rebuilding ADC Engine...");
        reconfigure_adc_hardware(last_channels_mask);
        _needs_hardware_reconfig = false;
        if (adc_sync_sem) xSemaphoreGive(adc_sync_sem);
        R_MUTEX_UNLOCK(adc_mutex);
      }
    }

    if (is_adc_running && adc_handle != NULL && !_needs_hardware_reconfig) {
      esp_err_t ret = adc_continuous_read(adc_handle, raw_buffer, CONFIG_ESP_ADC_FRAME_SIZE_BYTES, &ret_num, MSEC(10));

      if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
        uint32_t channel_sums[10] = {0};
        uint16_t channel_counts[10] = {0};

        for (int i = 0; i < (int)ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
          adc_digi_output_data_t* p = (adc_digi_output_data_t*)&raw_buffer[i];
          uint8_t chan = p->type2.channel;

          if (chan < 10) {
            channel_sums[chan] += p->type2.data;
            channel_counts[chan]++;
          }
        }

        if (R_MUTEX_LOCK(gpio_mutex, MSEC(5)) == pdTRUE) {
          for (int pin = 0; pin < GPIO_NUM_MAX; pin++) {
            esp_pin_obj_t* pin_obj = pin_obj_get(pin);
            if (pin_obj && pin_obj->pin_mode == SYS_IO_MODE_ADC) {
              adc_unit_t unit;
              adc_channel_t chan;
              if (adc_continuous_io_to_channel(pin, &unit, &chan) == ESP_OK && unit == ADC_UNIT_1) {
                process_adc_channel(pin, chan, channel_sums[chan], channel_counts[chan], pin_obj);
              }
            }
          }
          R_MUTEX_UNLOCK(gpio_mutex);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// =========================================================================
// INITIALIZATION
// =========================================================================

esp_err_t esp_adc_start() {
  if (adc_processing_task == NULL) {
    R_TASK_START_ON_CORE(adc_processing_task, adc_processing_task_function, NULL, CONFIG_PRIORITY_ESP_ADC_TASK, 0);
  }
  return ESP_OK;
}

esp_err_t esp_adc_get_mv(uint8_t pin, uint32_t* out_mv) {
  if (out_mv == NULL || pin >= GPIO_NUM_MAX) return ESP_ERR_INVALID_ARG;
  esp_pin_obj_t* pin_obj = pin_obj_get(pin);
  if (pin_obj == NULL || pin_obj->pin_mode != SYS_IO_MODE_ADC) {
    return ESP_ERR_INVALID_ARG;
  }
  *out_mv = gpio_esp_ctx.base.is_frozen ? pin_obj->hw.adc_cfg.adc_cached_mv : pin_obj->hw.adc_cfg.adc_last_read_mv;
  return ESP_OK;
}
