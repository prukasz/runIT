#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"


/**
 * @brief Initializes the ADC DMA system, calibration, and spawns the background processing task.
 * 
 * Call this exactly once during system startup.
 * 
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t esp_adc_start(void);

/**
 * @brief Configures and activates a specific ADC channel with user-defined thresholds and callbacks.
 * 
 * @param channel The ADC1 channel number (0-9).
 * @param sys_io_adc_int_config Pointer to a sys_io_adc_int_config_t structure.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t esp_adc_add_pin(uint8_t channel, void* sys_io_adc_int_config);

/**
 * @brief Dynamically changes which ADC channels are actively being sampled using a bitmask.
 * 
 * @param channel_mask A 10-bit bitmask representing the active channels.
 *                     (e.g., 0x03 activates channels 0 and 1)
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t set_active_adc_channels(uint16_t channel_mask);

/**
 * @brief Freezes or unfreezes the updating of the cached ADC value and halts alert triggering.
 * 
 * The hardware DMA and IIR filter keep running in the background, so unfreezing 
 * will instantly provide a highly accurate, smoothed value without lag.
 * 
 * @param channel The ADC1 channel number (0-9).
 * @param freeze true to freeze updates/alerts, false to resume normal operation.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if channel is invalid/uninitialized.
 */
void esp_adc_freeze(bool freeze);

/**
 * @brief Safely retrieves the currently cached/frozen voltage for a given channel.
 * 
 * @param channel The ADC1 channel number (0-9).
 * @param out_mv Pointer to a variable where the millivolt reading will be stored.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if channel uninitialized.
 */
esp_err_t esp_adc_get_mv(uint8_t channel, uint16_t* out_mv);
