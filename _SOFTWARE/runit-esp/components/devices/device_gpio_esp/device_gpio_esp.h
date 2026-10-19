#pragma once

#include "sys_device.h"
#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ESP32 native GPIO configuration struct (singleton, takes no configuration).
 */
typedef sys_device_none_t gpio_esp_cfg_t;

#ifdef __cplusplus
}
#endif
