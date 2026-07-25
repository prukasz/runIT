#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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

#include "device_gpio_esp.h"

typedef struct {
  sys_device_adapter_base_t base;
  d_gpio_esp_cfg_t cfg;
  uint64_t cached_inputs;
  uint64_t pending_outputs;
  uint64_t current_outputs;
} gpio_esp_ctx_t;

// Static pin pool: one fixed-size slot per GPIO, indexed by pin number.
// `configured_pins` is the source of truth for "is this slot live" - a slot's
// struct contents are only meaningful while its bit is set. Callers must hold
// gpio_mutex around any read/write of pin_pool[] or configured_pins.
extern esp_pin_obj_t pin_pool[GPIO_NUM_MAX];
extern uint64_t configured_pins;

// Returns the pin's object if configured, else NULL. Caller must hold gpio_mutex.
static inline esp_pin_obj_t* pin_obj_get(sys_io_pin_num_t pin) {
  return (configured_pins & (1ULL << pin)) ? &pin_pool[pin] : NULL;
}

extern gpio_esp_ctx_t gpio_esp_ctx;
extern SemaphoreHandle_t gpio_mutex;