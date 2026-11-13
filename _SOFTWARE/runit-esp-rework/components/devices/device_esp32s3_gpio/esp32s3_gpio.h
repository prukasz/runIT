#pragma once

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "freertos/FreeRTOS.h"
#include "sys_io.h"

/*
 * Private to this component, nothing else includes it (no INCLUDE_DIRS): the pins of the ESP32-S3 as one device,
 * shared by the driver (device_esp32s3_gpio.c), its PWM (esp32s3_gpio_pwm.c) and its ADC task (esp32s3_gpio_adc.c).
 * The pins' state is a static table, not the driver's struct: the pin ISR and the ADC task read it too.
 */

/* ----------------------------------------------------------------------------
 * A pin
 * ------------------------------------------------------------------------- */

/** @brief An ADC pin: its ADC1 channel and the ADC task's filtered reading. */
typedef struct {
  adc_channel_t channel;          /**< Set by MODE */
  adc_cali_handle_t cali_handle;  /**< raw -> mV */
  float raw_filtered;             /**< IIR-filtered raw reading */
  uint16_t last_mV;               /**< The latest reading: what VOLTAGE answers */
  bool primed;                    /**< The filter was seeded with the first average (else it starts at 0 mV) */
  bool alert_active;              /**< The armed condition fired; cleared once the reading is back past the hysteresis */
  uint16_t past_count;            /**< Readings in a row meeting the condition (event_counter_threshold) */
} esp32s3_gpio_adc_t;

/** @brief A PWM pin. Duty is on the contract's scale (0..IO_PWM_DUTY_MAX), whatever resolution its timer runs at. */
typedef struct {
  uint32_t frequency_Hz;  /**< Of the pin's timer; 0 until one is bound */
  uint16_t duty;          /**< Applied */
  uint8_t channel;        /**< LEDC channel, reserved by MODE */
  uint8_t timer;          /**< LEDC timer, ESP32S3_GPIO_PWM_TIMER_NONE until the first frequency or duty */
  uint16_t saved_duty;    /**< The duty to bring back after a suspend */
} esp32s3_gpio_pwm_t;

/** @brief One configured pin. */
typedef struct {
  uint8_t io_num;
  sys_io_mode_e pin_mode;
  union {
    esp32s3_gpio_adc_t adc;
    esp32s3_gpio_pwm_t pwm;
  } hw;
  bool suspended;          /**< Made safe by SUSPEND: PWM at 0, push-pull low, open-drain released */
  bool saved_level;        /**< The output level to bring back after a suspend */
  sys_io_intr_cfg_t intr;  /**< INTR: edges (the ISR) on a digital pin, thresholds (the ADC task) on an ADC pin */
  int64_t last_isr_us;     /**< The last edge published, for the debounce */
} esp32s3_gpio_pin_t;

/**
 * @brief The pin's state if it is configured, else NULL. The lock of the pins held.
 * @param[in] pin GPIO number
 * @return The pin's state, or NULL
 */
esp32s3_gpio_pin_t* esp32s3_gpio_pin(uint8_t pin);

/** @brief The lock of the pins, against the ADC task and the ISR's readers (the device's ops are serialized by the
 * registry). Never held while waiting for the ADC's own lock. */
void esp32s3_gpio_lock(void);
bool esp32s3_gpio_try_lock(TickType_t ticks);
void esp32s3_gpio_unlock(void);

/**
 * @brief The device's ID: the source of its pin events and the `id` of its errors (the ISR and the ADC task have no
 * struct).
 * @return The ID, SYS_DEVICE_ID_NONE while there is no device
 */
uint8_t esp32s3_gpio_device_id(void);

/* ----------------------------------------------------------------------------
 * PWM (esp32s3_gpio_pwm.c)
 *
 * PWM on the pins through LEDC (low-speed mode, APB clock). LEDC is shared only with other LEDC users (MCPWM, RMT,
 * GPTimer have their own timers); the Kconfig masks keep timers and channels for them. Two pools, shared by every PWM
 * pin:
 * - channels in CONFIG_ESP32S3_GPIO_PWM_CHANNEL_MASK: one per PWM pin, reserved by esp32s3_gpio_pwm_claim();
 * - timers in CONFIG_ESP32S3_GPIO_PWM_TIMER_MASK: one per frequency. Pins at one frequency share a timer, so there are
 *   as many frequencies at once as timers. A pin binds a timer at its first frequency or duty write (a duty without a
 *   frequency first runs at CONFIG_ESP32S3_GPIO_PWM_DEFAULT_FREQ_HZ).
 * A new frequency joins the timer already running it, else retunes the pin's own timer when no other pin uses it, else
 * takes a free timer; with none free it is ERR_IO_PWM_TIMERS_EXHAUSTED and the pin keeps its frequency. Every function
 * expects the lock of the pins held.
 * ------------------------------------------------------------------------- */

#define ESP32S3_GPIO_PWM_SRC_CLK_HZ 80000000u  // the source clock of every timer (LEDC_USE_APB_CLK)

/* Frequency limits. Low: at 14-bit resolution the divider (at most 1023.99) reaches ~4.8 Hz. High: the resolution is
 * log2(80 MHz / frequency), kept at 4 bits (16 duty steps) or more. */
#define ESP32S3_GPIO_PWM_FREQ_MIN_HZ 5u
#define ESP32S3_GPIO_PWM_FREQ_MAX_HZ (ESP32S3_GPIO_PWM_SRC_CLK_HZ >> 4)

#define ESP32S3_GPIO_PWM_TIMER_NONE 0xFFu

/**
 * @brief Reserves a channel for a pin going into PWM mode. Its output stays off (no timer) until the first frequency
 * or duty write.
 * @param[in,out] pin The pin
 * @return ERR_IO_PWM_CHANNELS_EXHAUSTED when every channel is in use
 */
MUST_USE err_h esp32s3_gpio_pwm_claim(esp32s3_gpio_pin_t* pin);

/**
 * @brief Runs the pin at frequency_Hz (ESP32S3_GPIO_PWM_FREQ_MIN_HZ..ESP32S3_GPIO_PWM_FREQ_MAX_HZ, checked by the
 * caller), keeping its duty.
 * @param[in,out] pin          The pin
 * @param[in]     frequency_Hz PWM frequency in Hz
 * @return ERR_IO_PWM_TIMERS_EXHAUSTED when the frequency needs a timer and none is free
 */
MUST_USE err_h esp32s3_gpio_pwm_set_frequency(esp32s3_gpio_pin_t* pin, uint32_t frequency_Hz);

/**
 * @brief Sets the pin's duty (0..IO_PWM_DUTY_MAX, checked by the caller); binds a timer at the default frequency if
 * the pin has none yet.
 * @param[in,out] pin  The pin
 * @param[in]     duty 0..IO_PWM_DUTY_MAX
 * @return ERR_IO_PWM_TIMERS_EXHAUSTED when that needs a timer and none is free
 */
MUST_USE err_h esp32s3_gpio_pwm_set_duty(esp32s3_gpio_pin_t* pin, uint16_t duty);

/**
 * @brief Stops the output (low), frees the channel and drops the pin from its timer (the last pin frees the timer).
 * Every step runs.
 * @param[in,out] pin The pin
 * @return The first failure of the steps
 */
MUST_USE err_h esp32s3_gpio_pwm_release(esp32s3_gpio_pin_t* pin);

/* ----------------------------------------------------------------------------
 * ADC (esp32s3_gpio_adc.c)
 *
 * The pins in ADC mode: one continuous conversion over their ADC1 channels, read by a task that filters each pin's
 * reading (esp32s3_gpio_adc_t.last_mV) and publishes the events of its armed thresholds, deferred.
 * ------------------------------------------------------------------------- */

/**
 * @brief Starts the ADC task, once per boot (it idles while no pin is in ADC mode).
 * @return The error of the task start
 */
MUST_USE err_h esp32s3_gpio_adc_start(void);

/**
 * @brief Rebuilds the conversion for the pins in ADC mode now: after a pin enters or leaves ADC mode. The lock of the
 * pins not held (it takes it).
 * @return The error of the ADC driver
 */
MUST_USE err_h esp32s3_gpio_adc_update_channels(void);
