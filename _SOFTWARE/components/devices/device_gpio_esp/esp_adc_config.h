#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sys_io.h"

esp_err_t esp_adc_start();

/**
 * @brief Dynamically changes active channels
 *
 * @return ESP_OK on success, or an error code on failure of driver
 */
esp_err_t esp_adc_update_active_channels(void);

/**
 * @brief Get voltage from cached values of active pin
 * @param pin The GPIO pin number.
 * @param out_mv Pointer to result
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if pin uninitialized.
 */
esp_err_t esp_adc_get_mv(uint8_t pin, uint32_t* out_mv);
