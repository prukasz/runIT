#pragma once

#include "sys_error.h"

/**
 * @brief Configuration for native ESP32 GPIO device.
 */
typedef struct d_gpio_esp_cfg_t {
  uint8_t device_id; /**< MUST be first member */
} d_gpio_esp_cfg_t;

/**
 * @brief Register and install the native ESP32 GPIO device.
 *
 * @param cfg Pointer to device configuration struct
 * @return err_h Status report of the operation.
 */
err_h d_gpio_esp_create(const d_gpio_esp_cfg_t* cfg);

