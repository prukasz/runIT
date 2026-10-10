#pragma once

#include "sys_device.h"
#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create struct: none (CREATE = [0x05][id u16][op 0x0000][type u16 = SYS_DEVICE_TYPE_GPIO_ESP]).
 * The ESP32-S3's pins are a hardware singleton: a second instance is refused
 * with ERR_DEV_ID_TAKEN (the id of the one that exists).
 */
typedef sys_device_none_t gpio_esp_cfg_t;

#ifdef __cplusplus
}
#endif
