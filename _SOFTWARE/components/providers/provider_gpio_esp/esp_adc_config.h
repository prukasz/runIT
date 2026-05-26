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
 * @return ESP_OK on success, INVALID_ARG(channel), ESP_ERR_NO_MEM(no mem), other codes form configs funcs
 */
esp_err_t esp_adc_add_intr_pin(uint8_t channel, void* sys_io_adc_int_config);

/**
 * @brief Dynamically changes active channels, based on bitmask 
 * 
 * @param channel_mask channesl bitmask
 * @return ESP_OK on success, or an error code on failure of driver
 */
esp_err_t esp_adc_set_active_channels(uint16_t channel_mask);

/**
 * @brief Freezes updates of cached value untill unblocked
 * @param freeze or not chache updates
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if channel is invalid/uninitialized.
 */
void esp_adc_freeze_results(bool freeze);

/**
 * @brief Get voltage from cached values ig channel active 
 * @param channel The ADC1 channel number (0-9).
 * @param out_mv Pointer to result 
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if channel uninitialized.
 */
esp_err_t esp_adc_get_mv(uint8_t channel, uint16_t* out_mv);

/**
 * @brief Bind a pin object to an ADC channel for DMA caching
 */
void esp_adc_bind_pin_obj(uint8_t channel, void* pin_obj);
