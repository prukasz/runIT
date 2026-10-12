#include "esp_adc_config.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_err.h"
#include "errors.h"
#include "gpio_esp_pin.h"
#include "sys_event.h"
#include "utils.h"

#define RETURN_ON_ERROR(x)                   \
  do {                                       \
    esp_err_t __err_rc = (x);                \
    if (__err_rc != ESP_OK) return __err_rc; \
  } while (0)

#define ADC_FRAME_SIZE_BYTES 256
#define ADC_SAMPLE_RATE_HZ 20000
#define ADC_IIR_ALPHA 0.1f
#define ADC_TASK_PRIORITY 5
#define ADC_CHANNELS 10  // ADC1 on the ESP32-S3

U_MUTEX_DEFINE(adc_mutex);
U_TASK_DEFINE(adc_processing_task, 4096);
U_BINARY_SEM_DEFINE(adc_sync_sem);

static adc_continuous_handle_t adc_handle = NULL;
static uint16_t last_channels_mask = 0;
static bool is_adc_running = false;

static volatile bool _needs_hardware_reconfig = false;

// Safely tears down and rebuilds the ADC hardware (background task, adc_mutex held).
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

  adc_digi_pattern_config_t adc_pattern[ADC_CHANNELS];
  uint8_t pattern_count = 0;

  for (uint8_t i = 0; i < ADC_CHANNELS; i++) {
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
      .conv_frame_size = ADC_FRAME_SIZE_BYTES,
  };
  RETURN_ON_ERROR(adc_continuous_new_handle(&handle_cfg, &adc_handle));

  adc_continuous_config_t adc_config = {
      .pattern_num = pattern_count,
      .adc_pattern = adc_pattern,
      .sample_freq_hz = ADC_SAMPLE_RATE_HZ,
      .conv_mode = ADC_CONV_SINGLE_UNIT_1,
      .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
  };

  RETURN_ON_ERROR(adc_continuous_config(adc_handle, &adc_config));
  RETURN_ON_ERROR(adc_continuous_start(adc_handle));
  is_adc_running = true;
  return ESP_OK;
}

// ADC1 channels of the pins in ADC mode. Takes gpio_mutex.
static uint16_t compute_active_channels_mask(void) {
  uint16_t mask = 0;
  if (U_MUTEX_LOCK(gpio_mutex, WAIT_FOREVER) == pdTRUE) {
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
    U_MUTEX_UNLOCK(gpio_mutex);
  }
  return mask;
}

esp_err_t esp_adc_update_active_channels(void) {
  if (U_MUTEX_LOCK(adc_mutex, WAIT_FOREVER) != pdTRUE) return ESP_FAIL;
  last_channels_mask = compute_active_channels_mask();

  // Consume any leftover semaphore token to prevent an early return
  xSemaphoreTake(adc_sync_sem, 0);

  _needs_hardware_reconfig = true;
  U_MUTEX_UNLOCK(adc_mutex);

  xSemaphoreTake(adc_sync_sem, WAIT_FOREVER);
  return ESP_OK;
}

// Filters the new samples of one pin and converts them to mV (gpio_mutex held).
static void process_adc_channel(uint32_t sum, uint16_t count, esp_pin_obj_t* pin_obj) {
  if (count == 0) return;

  float coarse_raw = (float)sum / count;
  pin_adc_data_t* adc_cfg = &pin_obj->hw.adc_cfg;
  if (!adc_cfg->primed) {
    // Seeded with the first average: from 0 the filter would take ~30 readings to settle, and a FALLING / window
    // alert armed right after SET_MODE would fire on that climb.
    adc_cfg->internal_raw_filtered = coarse_raw;
    adc_cfg->primed = true;
  }
  adc_cfg->internal_raw_filtered = (ADC_IIR_ALPHA * coarse_raw) + ((1.0f - ADC_IIR_ALPHA) * adc_cfg->internal_raw_filtered);

  if (adc_cfg->cali_handle == NULL) return;

  int voltage_mV = 0;
  // Background task, no caller to report to: keep the last good reading.
  if (adc_cali_raw_to_voltage(adc_cfg->cali_handle, (int)adc_cfg->internal_raw_filtered, &voltage_mV) != ESP_OK) return;

  adc_cfg->adc_last_read_mV = (uint16_t)voltage_mV;
}

/* Thresholds of an armed ADC pin (gpio_mutex held), on its latest reading. Fires once the condition has held for
 * event_counter_threshold readings in a row (0 counts as 1), then stays quiet until the reading is back past the
 * hysteresis. Returns true and fills *ev when an event is due: the caller posts it after releasing gpio_mutex. */
static bool check_thresholds(esp_pin_obj_t* pin_obj, sys_event_t* ev) {
  pin_adc_data_t* adc_cfg = &pin_obj->hw.adc_cfg;
  const sys_io_intr_cfg_t* intr = &pin_obj->intr;
  if (intr->mode == SYS_IO_INTR_DISABLE) {
    return false;
  }
  const int32_t mV = adc_cfg->adc_last_read_mV;
  const int32_t up = intr->adc.threshold_up_mV;
  const int32_t down = intr->adc.threshold_down_mV;
  const int32_t hyst = intr->adc.hysteresis_mV;
  bool met = false;
  bool cleared = false;
  uint16_t event = 0;
  uint8_t flags = 0;
  switch (intr->mode) {
    case SYS_IO_INTR_ADC_WINDOW_OUTSIDE:
      met = mV >= up || mV <= down;
      cleared = mV < up - hyst && mV > down + hyst;
      event = IO_EV_ADC_OUTSIDE;
      flags = IO_EV_ADC_OUTSIDE_FLAGS;
      break;
    case SYS_IO_INTR_ADC_WINDOW_INSIDE:
      met = mV <= up && mV >= down;
      cleared = mV > up + hyst || mV < down - hyst;
      event = IO_EV_ADC_INSIDE;
      flags = IO_EV_ADC_INSIDE_FLAGS;
      break;
    case SYS_IO_INTR_MODE_RISING_EDGE:
      met = mV >= up;
      cleared = mV < up - hyst;
      event = IO_EV_RISING;
      flags = IO_EV_RISING_FLAGS;
      break;
    case SYS_IO_INTR_MODE_FALLING_EDGE:
      met = mV <= down;
      cleared = mV > down + hyst;
      event = IO_EV_FALLING;
      flags = IO_EV_FALLING_FLAGS;
      break;
    default:
      return false;  // CONFIGURE_INTR refuses the other modes on an ADC pin
  }

  if (adc_cfg->alert_active) {
    if (cleared) {
      adc_cfg->alert_active = false;
    }
    return false;
  }
  if (!met) {
    adc_cfg->past_count = 0;
    return false;
  }
  const uint16_t needed = intr->adc.event_counter_threshold > 0 ? intr->adc.event_counter_threshold : 1;
  if (++adc_cfg->past_count < needed) {
    return false;
  }
  adc_cfg->alert_active = true;
  adc_cfg->past_count = 0;
  *ev = (sys_event_t){.source = gpio_esp_device_id, .event = event, .channel = pin_obj->io_num, .flags = flags,
                      .value = mV};
  return true;
}

static void adc_processing_task_function(void* pvParameters) {
  (void)pvParameters;
  uint8_t raw_buffer[ADC_FRAME_SIZE_BYTES];
  uint32_t ret_num = 0;

  while (1) {
    if (_needs_hardware_reconfig) {
      if (U_MUTEX_LOCK(adc_mutex, WAIT_FOREVER) == pdTRUE) {
        // A failed rebuild leaves the ADC stopped; the caller has no way to hear of it, the readings just stay as they were.
        (void)reconfigure_adc_hardware(last_channels_mask);
        _needs_hardware_reconfig = false;
        xSemaphoreGive(adc_sync_sem);
        U_MUTEX_UNLOCK(adc_mutex);
      }
    }

    if (is_adc_running && adc_handle != NULL && !_needs_hardware_reconfig) {
      esp_err_t ret = adc_continuous_read(adc_handle, raw_buffer, ADC_FRAME_SIZE_BYTES, &ret_num, MSEC(10));

      if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
        uint32_t channel_sums[ADC_CHANNELS] = {0};
        uint16_t channel_counts[ADC_CHANNELS] = {0};

        for (int i = 0; i < (int)ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
          adc_digi_output_data_t* p = (adc_digi_output_data_t*)&raw_buffer[i];
          uint8_t chan = p->type2.channel;

          if (chan < ADC_CHANNELS) {
            channel_sums[chan] += p->type2.data;
            channel_counts[chan]++;
          }
        }

        sys_event_t due[ADC_CHANNELS];  // one ADC1 channel per pin, so at most one event each
        uint8_t due_count = 0;
        if (U_MUTEX_LOCK(gpio_mutex, MSEC(5)) == pdTRUE) {
          for (int pin = 0; pin < GPIO_NUM_MAX; pin++) {
            esp_pin_obj_t* pin_obj = pin_obj_get(pin);
            if (pin_obj && pin_obj->pin_mode == SYS_IO_MODE_ADC) {
              adc_unit_t unit;
              adc_channel_t chan;
              if (adc_continuous_io_to_channel(pin, &unit, &chan) == ESP_OK && unit == ADC_UNIT_1) {
                process_adc_channel(channel_sums[chan], channel_counts[chan], pin_obj);
                if (channel_counts[chan] > 0 && due_count < ADC_CHANNELS && check_thresholds(pin_obj, &due[due_count])) {
                  due_count++;
                }
              }
            }
          }
          U_MUTEX_UNLOCK(gpio_mutex);
        }
        /* SYS_EVENT_DEFER: SET_MODE / RESET wait for this task (esp_adc_update_active_channels) while
           holding the device lock, so a listener run here that needs the lock would deadlock. Nobody to
           return to: a failed publish (queue full) is reported. */
        for (uint8_t i = 0; i < due_count; i++) {
          due[i].flags |= SYS_EVENT_DEFER;
          err_report(sys_event_publish(&due[i]));
        }
      }
    }
    vTaskDelay(MSEC(10));
  }
}

esp_err_t esp_adc_start(void) {
  if (adc_processing_task == NULL) {
    U_TASK_START_ON_CORE(adc_processing_task, adc_processing_task_function, NULL, ADC_TASK_PRIORITY, 0);
  }
  return ESP_OK;
}

esp_err_t esp_adc_get_mV(uint8_t pin, int32_t* out_mV) {
  if (out_mV == NULL || pin >= GPIO_NUM_MAX) return ESP_ERR_INVALID_ARG;
  esp_err_t err = ESP_OK;
  if (U_MUTEX_LOCK(gpio_mutex, WAIT_FOREVER) != pdTRUE) return ESP_FAIL;
  esp_pin_obj_t* pin_obj = pin_obj_get(pin);
  if (pin_obj == NULL || pin_obj->pin_mode != SYS_IO_MODE_ADC) {
    err = ESP_ERR_INVALID_ARG;
  } else {
    *out_mV = pin_obj->hw.adc_cfg.adc_last_read_mV;
  }
  U_MUTEX_UNLOCK(gpio_mutex);
  return err;
}
