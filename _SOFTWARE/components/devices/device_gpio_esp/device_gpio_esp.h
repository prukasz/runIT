#pragma once

#include "sys_device.h"
#include "sys_io.h"

//#device
//@alias ESP32 native GPIO
//@description ESP32-S3 GPIO. Some pins are reserved for internal use, the rest are free for yours.
//@protocol native
//@tags gpio io adc voltage pwm
//@datasheet https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
//@pins IO @one-of [0..21, 26..48] @alias GPIO pin
//@event IO_EV_RISING IO_EV_FALLING IO_EV_ADC_OUTSIDE IO_EV_ADC_INSIDE @channel GPIO pin
//@pwm-frequencies CONFIG_DEVICE_GPIO_ESP_PWM_TIMER_MASK @count-bits

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
