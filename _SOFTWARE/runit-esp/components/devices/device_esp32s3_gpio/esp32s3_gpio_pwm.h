#pragma once

#include "errors.h"
#include "esp32s3_gpio_pin.h"

/*
 * PWM on native GPIO through LEDC (low-speed mode, APB clock). LEDC is not
 * shared with other peripherals (MCPWM, RMT, GPTimer have their own timers),
 * only with other LEDC users; the Kconfig masks keep timers / channels for them.
 *
 * Two pools, shared by every PWM pin:
 * - channels in CONFIG_DEVICE_ESP32S3_GPIO_PWM_CHANNEL_MASK: one per PWM pin,
 *   reserved by esp32s3_gpio_pwm_claim().
 * - timers in CONFIG_DEVICE_ESP32S3_GPIO_PWM_TIMER_MASK: one per distinct frequency.
 *   Pins at the same frequency share a timer, so there are as many frequencies at
 *   once as timers. A pin binds a timer at its first frequency or duty write (the
 *   first duty write without a frequency uses CONFIG_DEVICE_ESP32S3_GPIO_PWM_DEFAULT_FREQ_HZ).
 *
 * Changing a pin's frequency joins the timer already running it, else retunes
 * the pin's own timer when no other pin uses it, else takes a free timer. With
 * none free it answers ERR_IO_PWM_TIMERS_EXHAUSTED and the pin keeps its old frequency.
 *
 * Every function expects the lock of the pins held.
 */

/* Source clock of every timer (LEDC_USE_APB_CLK). */
#define ESP32S3_GPIO_PWM_SRC_CLK_HZ 80000000u

/* Frequency limits. Low: at 14-bit resolution the divider (max 1023.99)
   reaches ~4.8 Hz. High: resolution is log2(80 MHz / frequency), kept at 4 bits
   (16 duty steps) or more. */
#define ESP32S3_GPIO_PWM_FREQ_MIN_HZ 5u
#define ESP32S3_GPIO_PWM_FREQ_MAX_HZ (ESP32S3_GPIO_PWM_SRC_CLK_HZ >> 4)

#define ESP32S3_GPIO_PWM_TIMER_NONE 0xFFu

/**
 * @brief Reserves a channel for a pin being set to PWM mode. Its output stays off (no timer) until the first
 *        frequency or duty write.
 * @return ERR_IO_PWM_CHANNELS_EXHAUSTED when every channel is in use.
 */
MUST_USE err_h esp32s3_gpio_pwm_claim(esp32s3_gpio_pin_t* pin);

/**
 * @brief Runs the pin at frequency_Hz (ESP32S3_GPIO_PWM_FREQ_MIN_HZ..ESP32S3_GPIO_PWM_FREQ_MAX_HZ, checked by the
 *        caller), keeping its duty.
 * @return ERR_IO_PWM_TIMERS_EXHAUSTED when the frequency needs a timer and none is free.
 */
MUST_USE err_h esp32s3_gpio_pwm_set_frequency(esp32s3_gpio_pin_t* pin, uint32_t frequency_Hz);

/**
 * @brief Sets the pin's duty (0..IO_PWM_DUTY_MAX, checked by the caller). Binds a timer at the default frequency if
 *        the pin has none yet.
 * @return ERR_IO_PWM_TIMERS_EXHAUSTED when that needs a timer and none is free.
 */
MUST_USE err_h esp32s3_gpio_pwm_set_duty(esp32s3_gpio_pin_t* pin, uint16_t duty);

/**
 * @brief Stops the output (low), frees the channel and drops the pin from its timer (the last pin releases the
 *        timer). Every step runs; the first failure is returned.
 */
MUST_USE err_h esp32s3_gpio_pwm_release(esp32s3_gpio_pin_t* pin);
