#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "sys_device.h"
#include "sys_io.h"

// 2. ADC-specific storage
typedef struct {
  uint16_t adc_last_read_mv;
  uint16_t adc_cached_mv;
  float internal_raw_filtered;
  bool alert_was_triggered;
  adc_cali_handle_t cali_handle;
} pin_adc_data_t;

// 3. PWM-specific storage
typedef struct {
  uint32_t freq_hz;
  uint32_t duty_cycle;
  uint8_t timer_num;
  uint8_t channel_num;
} pin_pwm_data_t;

// 4. The Master Unified Pin Object
typedef struct {
  sys_io_pin_num_t io_num;
  sys_io_mode_e pin_mode;
  sys_io_intr_config_t intr_config;

  union {
    gpio_config_t gpio_cfg;
    pin_adc_data_t adc_cfg;
    pin_pwm_data_t pwm_cfg;
  } hw;
  uint64_t last_isr_time;
} esp_pin_obj_t;

typedef struct {
  sys_device_adapter_base_t base;
  uint64_t cached_inputs;
  uint64_t pending_outputs;
  uint64_t current_outputs;
  uint64_t current_inputs;
} gpio_esp_ctx_t;

extern esp_pin_obj_t* pin_registry[GPIO_NUM_MAX];
extern gpio_esp_ctx_t gpio_esp_ctx;
extern SemaphoreHandle_t gpio_mutex;