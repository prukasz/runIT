#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sys_io.h"

// ADC-specific storage
typedef struct {
  uint16_t adc_last_read_mV;
  float internal_raw_filtered;
  adc_cali_handle_t cali_handle;
  bool primed;            /* the filter was seeded with the first average (else it starts at 0 mV) */
  bool alert_active;      /* the armed condition fired; cleared when the reading is back past the hysteresis */
  uint16_t past_count;    /* consecutive readings meeting the condition (event_counter_threshold) */
} pin_adc_data_t;

// PWM-specific storage (esp_pwm.h). Duty is on the contract scale
// (0..IO_PWM_DUTY_MAX), whatever resolution the pin's timer runs at.
typedef struct {
  uint32_t frequency_Hz; /* of the pin's timer; 0 until one is bound */
  uint16_t duty;         /* applied */
  uint8_t channel;       /* LEDC channel, reserved by set_mode */
  uint8_t timer;         /* LEDC timer, ESP_PWM_TIMER_NONE until the first frequency or duty */
  uint16_t saved_duty;   /* duty to bring back after a suspend */
} pin_pwm_data_t;

// One configured pin.
typedef struct {
  uint8_t io_num;
  sys_io_mode_e pin_mode;
  union {
    gpio_config_t gpio_cfg;
    pin_adc_data_t adc_cfg;
    pin_pwm_data_t pwm_cfg;
  } hw;
  bool suspended;    /* made safe by SUSPEND: PWM at 0, push-pull low, open-drain released */
  bool saved_level;  /* output level to bring back after a suspend */
  sys_io_intr_cfg_t intr;  /* CONFIGURE_INTR: edges (ISR) on a digital pin, thresholds (ADC task) on an ADC pin */
  int64_t last_isr_us;     /* last accepted edge, for debounce */
} esp_pin_obj_t;

// Static pin pool: one slot per GPIO, indexed by pin number. `configured_pins` is
// the source of truth for "is this slot live". Hold gpio_mutex around any
// read/write of pin_pool[] or configured_pins: the ADC task reads them too.
extern esp_pin_obj_t pin_pool[GPIO_NUM_MAX];
extern uint64_t configured_pins;
extern SemaphoreHandle_t gpio_mutex;
extern uint16_t gpio_esp_device_id;  // source of the pin events: the ISR and the ADC task have no struct

// The pin's object if configured, else NULL. Caller holds gpio_mutex.
static inline esp_pin_obj_t* pin_obj_get(uint8_t pin) {
  return (pin < GPIO_NUM_MAX && (configured_pins & (1ULL << pin))) ? &pin_pool[pin] : NULL;
}
