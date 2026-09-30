#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"
#include "shared_io_types.h"
#include "soc/soc_caps.h"

/*
 * PWM on native GPIO through LEDC (low-speed mode, APB clock). LEDC is not
 * shared with other peripherals (MCPWM, RMT, GPTimer have their own timers),
 * only with other LEDC users; the Kconfig masks keep timers / channels for them.
 *
 * Two pools, shared by every PWM pin:
 * - channels in CONFIG_DEVICE_GPIO_ESP_PWM_CHANNEL_MASK: one per PWM pin,
 *   reserved by esp_pwm_claim().
 * - timers in CONFIG_DEVICE_GPIO_ESP_PWM_TIMER_MASK: one per distinct frequency.
 *   Pins at the same frequency share a timer, so at most ESP_PWM_TIMERS
 *   frequencies run at once. A pin binds a timer at its first frequency or duty
 *   write (the first duty write without a frequency uses
 *   CONFIG_DEVICE_GPIO_ESP_PWM_DEFAULT_FREQ_HZ).
 *
 * Changing a pin's frequency joins the timer already running it, else retunes
 * the pin's own timer when no other pin uses it, else takes a free timer. With
 * none free it returns ESP_ERR_NOT_FOUND and the pin keeps its old frequency.
 *
 * Every function expects the caller to hold gpio_mutex.
 */

/* Duty scale of the device (12-bit, like PCA9685 and DRV8962's counts):
   0 = off, ESP_PWM_DUTY_FULL = always on. Converted to each timer's resolution. */
#define ESP_PWM_DUTY_FULL 4096u

/* Source clock of every timer (LEDC_USE_APB_CLK). */
#define ESP_PWM_SRC_CLK_HZ 80000000u

/* Frequency limits. Low: at 14-bit resolution the divider (max 1023.99)
   reaches ~4.8 Hz. High: resolution is log2(80 MHz / frequency), kept at 4 bits
   (16 duty steps) or more. */
#define ESP_PWM_FREQ_MIN_HZ 5u
#define ESP_PWM_FREQ_MAX_HZ (ESP_PWM_SRC_CLK_HZ >> 4)

#define ESP_PWM_TIMER_NONE 0xFFu

/* Timers / channels PWM pins may use (for error payloads and messages). */
#define ESP_PWM_TIMERS ((uint8_t)__builtin_popcount(CONFIG_DEVICE_GPIO_ESP_PWM_TIMER_MASK))
#define ESP_PWM_CHANNELS ((uint8_t)__builtin_popcount(CONFIG_DEVICE_GPIO_ESP_PWM_CHANNEL_MASK))

/**
 * @brief Reserve a channel for a pin being set to PWM mode. Its output stays
 *        off (no timer) until the first frequency or duty write.
 * @return ESP_ERR_NOT_FOUND when every channel is in use.
 */
esp_err_t esp_pwm_claim(esp_pin_obj_t* pin);

/**
 * @brief Run the pin at frequency_Hz (ESP_PWM_FREQ_MIN_HZ..ESP_PWM_FREQ_MAX_HZ,
 *        checked by the caller), keeping its duty.
 * @return ESP_ERR_NOT_FOUND when the frequency needs a timer and none is free.
 */
esp_err_t esp_pwm_set_frequency(esp_pin_obj_t* pin, uint32_t frequency_Hz);

/**
 * @brief Set the pin's duty (0..ESP_PWM_DUTY_FULL, checked by the caller).
 *        Binds a timer at the default frequency if the pin has none yet.
 * @return ESP_ERR_NOT_FOUND when that needs a timer and none is free.
 */
esp_err_t esp_pwm_set_duty(esp_pin_obj_t* pin, uint32_t duty);

/**
 * @brief Stop the output (low), free the channel and drop the pin from its
 *        timer (the last pin releases the timer). Every step runs; the first
 *        failure is returned.
 */
esp_err_t esp_pwm_release(esp_pin_obj_t* pin);
