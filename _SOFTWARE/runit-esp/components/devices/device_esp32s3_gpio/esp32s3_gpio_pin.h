#pragma once

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "freertos/FreeRTOS.h"
#include "sys_io.h"

/* Private to this component: the pins of the ESP32-S3, one device (the chip's own GPIO), shared by the device
 * (device_esp32s3_gpio.c), its PWM (esp32s3_gpio_pwm.c) and its ADC task (esp32s3_gpio_adc.c). */

// An ADC pin: its ADC1 channel and the ADC task's filtered reading.
typedef struct {
  adc_channel_t channel;          /* set by SET_MODE */
  adc_cali_handle_t cali_handle;  /* raw -> mV */
  float raw_filtered;             /* IIR-filtered raw reading */
  uint16_t last_mV;               /* the latest reading, what VOLTAGE answers */
  bool primed;                    /* the filter was seeded with the first average (else it starts at 0 mV) */
  bool alert_active;  /* the armed condition fired; cleared when the reading is back past the hysteresis */
  uint16_t past_count;  /* consecutive readings meeting the condition (event_counter_threshold) */
} pin_adc_data_t;

// A PWM pin (esp32s3_gpio_pwm.h). Duty is on the contract scale (0..IO_PWM_DUTY_MAX), whatever resolution the pin's
// timer runs at.
typedef struct {
  uint32_t frequency_Hz; /* of the pin's timer; 0 until one is bound */
  uint16_t duty;         /* applied */
  uint8_t channel;       /* LEDC channel, reserved by SET_MODE */
  uint8_t timer;         /* LEDC timer, ESP32S3_GPIO_PWM_TIMER_NONE until the first frequency or duty */
  uint16_t saved_duty;   /* duty to bring back after a suspend */
} pin_pwm_data_t;

// One configured pin.
typedef struct {
  uint8_t io_num;
  sys_io_mode_e pin_mode;
  union {
    pin_adc_data_t adc;
    pin_pwm_data_t pwm;
  } hw;
  bool suspended;          /* made safe by SUSPEND: PWM at 0, push-pull low, open-drain released */
  bool saved_level;        /* output level to bring back after a suspend */
  sys_io_intr_cfg_t intr;  /* INTR: edges (ISR) on a digital pin, thresholds (ADC task) on an ADC pin */
  int64_t last_isr_us;     /* last accepted edge, for debounce */
} esp32s3_gpio_pin_t;

/** @brief The pin's state if it is configured, else NULL. The lock held. */
esp32s3_gpio_pin_t* esp32s3_gpio_pin(uint8_t pin);

/** @brief The lock of the pins, against the ADC task (every call into the device is serialized by sys_device). */
void esp32s3_gpio_lock(void);
bool esp32s3_gpio_try_lock(TickType_t ticks);
void esp32s3_gpio_unlock(void);

/** @brief The device's ID: the source of its pin events (the ISR and the ADC task have no struct). */
uint16_t esp32s3_gpio_device_id(void);
