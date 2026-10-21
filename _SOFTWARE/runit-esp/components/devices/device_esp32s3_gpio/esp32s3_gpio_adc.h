#pragma once

#include "errors.h"

/* The ADC of the pins in ADC mode: one continuous conversion over their ADC1 channels, read by a task that filters
 * each pin's reading (esp32s3_gpio_pin_t.hw.adc.last_mV) and raises its armed thresholds as IO events. */

/** @brief Starts the ADC task (once; it idles while no pin is in ADC mode). */
MUST_USE err_h esp32s3_gpio_adc_start(void);

/** @brief Rebuilds the conversion for the pins now in ADC mode. After a pin enters or leaves ADC mode; the lock of the
 *  pins not held. */
MUST_USE err_h esp32s3_gpio_adc_update_channels(void);
