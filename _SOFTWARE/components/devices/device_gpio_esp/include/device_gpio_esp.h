#pragma once

#include "status.h"

/**
 * @brief Register and install the native ESP32 GPIO device.
 *
 * @param device_id The unique device ID to register under the device and IO
 * managers.
 * @return status_rep_t Status report of the operation.
 */
status_rep_t d_gpio_esp_create(uint8_t device_id);
