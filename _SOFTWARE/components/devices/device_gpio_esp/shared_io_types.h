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
  uint16_t adc_last_read_mV;
  float internal_raw_filtered;
  bool alert_was_triggered;
  adc_cali_handle_t cali_handle;
} pin_adc_data_t;

// 3. PWM-specific storage (esp_pwm.h). Duty is on the device scale
// (0..ESP_PWM_DUTY_FULL), whatever resolution the pin's timer runs at.
typedef struct {
  uint32_t frequency_Hz; /* of the pin's timer; 0 until one is bound */
  uint16_t duty;         /* applied */
  uint8_t channel;       /* LEDC channel, reserved by set_mode */
  uint8_t timer;         /* LEDC timer, ESP_PWM_TIMER_NONE until the first frequency or duty */
  uint16_t saved_duty;   /* duty to bring back after a suspend */
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
  bool suspended;    /* the output was made safe by device_suspend: PWM at 0, push-pull low, open-drain released */
  bool saved_level;  /* output level to bring back after a suspend */
} esp_pin_obj_t;

#include "device_gpio_esp.h"

// Instance state (the device is a hardware singleton: the pin pool below is the real state)
typedef struct {
  sys_device_base_t base;
} gpio_ctx_t;

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

extern uint8_t gpio_esp_device_id;  // device id for the ISR and the ADC task
extern SemaphoreHandle_t gpio_mutex;