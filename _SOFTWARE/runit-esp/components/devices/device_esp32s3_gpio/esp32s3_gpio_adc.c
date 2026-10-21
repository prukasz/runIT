#include "esp32s3_gpio_adc.h"

#include "esp32s3_gpio_pin.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_continuous.h"
#include "sys_event.h"
#include "utils.h"

#define ADC_FRAME_SIZE_BYTES 256
#define ADC_SAMPLE_RATE_HZ   20000
#define ADC_IIR_ALPHA        0.1f
#define ADC_TASK_PRIORITY    5
#define ADC_CHANNELS         10  // ADC1 on the ESP32-S3

/* Guards the conversion: a rebuild (a device op) and a read (the task) never overlap. Never taken with the lock of
 * the pins held, nor the other way round. */
U_MUTEX_DEFINE(adc_mutex);
U_TASK_DEFINE(adc_processing_task, 4096);

static adc_continuous_handle_t s_handle;  // NULL while no pin is in ADC mode

// The ADC1 channels of the pins in ADC mode.
static uint16_t active_channels(void) {
  uint16_t mask = 0;
  esp32s3_gpio_lock();
  for (uint8_t pin = 0; pin < GPIO_NUM_MAX; pin++) {
    const esp32s3_gpio_pin_t* p = esp32s3_gpio_pin(pin);
    if (p != NULL && p->pin_mode == SYS_IO_MODE_ADC) {
      mask |= (uint16_t)(1u << p->hw.adc.channel);
    }
  }
  esp32s3_gpio_unlock();
  return mask;
}

// The conversion torn down, then started again over `mask` (adc_mutex held). A failure leaves it stopped.
static err_h rebuild(uint16_t mask) {
  if (s_handle != NULL) {
    err_h err = ERR_ESP(adc_continuous_stop(s_handle));
    ERR_KEEP_FIRST(err, ERR_ESP(adc_continuous_deinit(s_handle)));
    s_handle = NULL;
    ERR_TRY(err);
  }
  if (mask == 0) {
    return NULL;
  }
  adc_digi_pattern_config_t pattern[ADC_CHANNELS];
  uint8_t count = 0;
  for (uint8_t ch = 0; ch < ADC_CHANNELS; ch++) {
    if (mask & (1u << ch)) {
      pattern[count++] = (adc_digi_pattern_config_t){
          .atten = ADC_ATTEN_DB_12, .channel = ch, .unit = ADC_UNIT_1, .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH};
    }
  }
  const adc_continuous_handle_cfg_t handle_cfg = {.max_store_buf_size = 4096, .conv_frame_size = ADC_FRAME_SIZE_BYTES};
  ERR_TRY_ESP(adc_continuous_new_handle(&handle_cfg, &s_handle));
  const adc_continuous_config_t cfg = {
      .pattern_num = count,
      .adc_pattern = pattern,
      .sample_freq_hz = ADC_SAMPLE_RATE_HZ,
      .conv_mode = ADC_CONV_SINGLE_UNIT_1,
      .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
  };
  err_h err = ERR_ESP(adc_continuous_config(s_handle, &cfg));
  if (err == NULL) {
    err = ERR_ESP(adc_continuous_start(s_handle));
  }
  if (err != NULL) {
    (void)adc_continuous_deinit(s_handle);  // the cause above is what gets reported
    s_handle = NULL;
  }
  return err;
}

err_h esp32s3_gpio_adc_update_channels(void) {
  const uint16_t mask = active_channels();
  (void)U_MUTEX_LOCK(adc_mutex, WAIT_FOREVER);
  const err_h err = rebuild(mask);
  (void)U_MUTEX_UNLOCK(adc_mutex);
  return err;
}

// Filters the new samples of one pin and converts them to mV (the lock of the pins held).
static void filter(pin_adc_data_t* adc, uint32_t sum, uint16_t count) {
  const float average = (float)sum / count;
  if (!adc->primed) {
    // Seeded with the first average: from 0 the filter would take ~30 readings to settle, and a FALLING / window
    // alert armed right after SET_MODE would fire on that climb.
    adc->raw_filtered = average;
    adc->primed = true;
  }
  adc->raw_filtered = (ADC_IIR_ALPHA * average) + ((1.0f - ADC_IIR_ALPHA) * adc->raw_filtered);
  int mV = 0;
  // No caller to report to: a failed conversion keeps the last good reading.
  if (adc_cali_raw_to_voltage(adc->cali_handle, (int)adc->raw_filtered, &mV) == ESP_OK) {
    adc->last_mV = (uint16_t)mV;
  }
}

/* Thresholds of an armed ADC pin (the lock of the pins held), on its latest reading. Fires once the condition has
 * held for event_counter_threshold readings in a row (0 counts as 1), then stays quiet until the reading is back past
 * the hysteresis. Returns true and fills *ev when an event is due: the caller posts it after the lock is released. */
static bool check_thresholds(esp32s3_gpio_pin_t* p, sys_event_t* ev) {
  pin_adc_data_t* adc = &p->hw.adc;
  const sys_io_intr_cfg_t* intr = &p->intr;
  const int32_t mV = adc->last_mV;
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
      return false;  // disarmed; INTR refuses the other modes on an ADC pin
  }

  if (adc->alert_active) {
    if (cleared) {
      adc->alert_active = false;
    }
    return false;
  }
  if (!met) {
    adc->past_count = 0;
    return false;
  }
  const uint16_t needed = intr->adc.event_counter_threshold > 0 ? intr->adc.event_counter_threshold : 1;
  if (++adc->past_count < needed) {
    return false;
  }
  adc->alert_active = true;
  adc->past_count = 0;
  *ev = (sys_event_t){
      .source = esp32s3_gpio_device_id(), .event = event, .channel = p->io_num, .flags = flags, .value = mV};
  return true;
}

// One frame: summed per channel, then every ADC pin filtered and its thresholds checked.
static void process(const uint8_t* frame, uint32_t len) {
  uint32_t sums[ADC_CHANNELS] = {0};
  uint16_t counts[ADC_CHANNELS] = {0};
  for (uint32_t i = 0; i < len; i += SOC_ADC_DIGI_RESULT_BYTES) {
    const adc_digi_output_data_t* d = (const adc_digi_output_data_t*)&frame[i];
    if (d->type2.channel < ADC_CHANNELS) {
      sums[d->type2.channel] += d->type2.data;
      counts[d->type2.channel]++;
    }
  }
  sys_event_t due[ADC_CHANNELS];  // one ADC1 channel per pin, so at most one event each
  uint8_t due_count = 0;
  if (esp32s3_gpio_try_lock(MSEC(5))) {
    for (uint8_t pin = 0; pin < GPIO_NUM_MAX; pin++) {
      esp32s3_gpio_pin_t* p = esp32s3_gpio_pin(pin);
      if (p == NULL || p->pin_mode != SYS_IO_MODE_ADC || counts[p->hw.adc.channel] == 0) {
        continue;
      }
      filter(&p->hw.adc, sums[p->hw.adc.channel], counts[p->hw.adc.channel]);
      if (due_count < ADC_CHANNELS && check_thresholds(p, &due[due_count])) {
        due_count++;
      }
    }
    esp32s3_gpio_unlock();
  }
  /* SYS_EVENT_DEFER: a listener run here that needs the registry lock would wait for an op that may be waiting for
     adc_mutex. Nobody to return to: a failed publish (queue full) is reported. */
  for (uint8_t i = 0; i < due_count; i++) {
    due[i].flags |= SYS_EVENT_DEFER;
    err_report(sys_event_publish(&due[i]));
  }
}

static void adc_task(void* arg) {
  (void)arg;
  uint8_t frame[ADC_FRAME_SIZE_BYTES];
  while (1) {
    uint32_t len = 0;
    esp_err_t rc = ESP_OK;
    (void)U_MUTEX_LOCK(adc_mutex, WAIT_FOREVER);
    if (s_handle != NULL) {
      rc = adc_continuous_read(s_handle, frame, sizeof(frame), &len, MSEC(10));
    }
    (void)U_MUTEX_UNLOCK(adc_mutex);
    // INVALID_STATE: the driver's buffer overflowed (the task fell behind), the frame read is still good.
    if ((rc == ESP_OK || rc == ESP_ERR_INVALID_STATE) && len > 0) {
      process(frame, len);
    }
    vTaskDelay(MSEC(10));
  }
}

err_h esp32s3_gpio_adc_start(void) {
  if (adc_processing_task == NULL) {
    U_TASK_START_ON_CORE(adc_processing_task, adc_task, NULL, ADC_TASK_PRIORITY, 0);
  }
  return NULL;
}
