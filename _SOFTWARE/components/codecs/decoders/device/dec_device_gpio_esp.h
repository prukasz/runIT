#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_gpio_esp/include/device_gpio_esp.h"

//#device device_gpio_esp
//  @title       ESP32 native GPIO
//  @description ESP32-S3 GPIO. Some pins are reserved for internal use, the rest are free for yours.
//  @protocol    native
//  @tags        gpio io adc voltage pwm
//  @datasheet   https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
//  @pwm-frequencies CONFIG_DEVICE_GPIO_ESP_PWM_TIMER_MASK @count-bits
//  @contract-provider $SYS_DEVICE_CONTRACT_IO

// ESP32-S3 GPIOs (SOC_GPIO_VALID_GPIO_MASK).
//#self-property PIN
//  @one-of   [0..21, 26..48]
//  @alias    GPIO Pin

//#property PIN-MODE
//  @enum-ref sys_io_mode_e
//  @one-of   [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP, $SYS_IO_MODE_INPUT_PULLDOWN,
//             $SYS_IO_MODE_OUTPUT_PUSH_PULL, $SYS_IO_MODE_OUTPUT_OPEN_DRAIN,
//             $SYS_IO_MODE_OUTPUT_OPEN_DRAIN_PULLUP, $SYS_IO_MODE_PWM, $SYS_IO_MODE_ADC]

//#contract packet_sys_io_reset_t
//  @alias       Reset pin
//  @description Releases the pin back to its unconfigured state.
//  @param pin   @arg PIN

//#contract packet_sys_io_set_mode_t
//  @alias       Configure pin mode
//  @description DAC mode isn't available on native GPIO - use a DAC53202 device. PWM takes one PWM
//               channel per pin: up to 8 pins at once on the ESP32-S3, fewer if the firmware keeps
//               channels for other drivers.
//  @param pin   @arg PIN
//  @param mode  @arg PIN-MODE

//#contract packet_sys_io_configure_intr_t
//  @alias       Configure pin interrupt
//  @description Digital edge-triggered interrupt (not the ADC window comparator, which native GPIO
//               doesn't have). Link it to the program or an action with an event subscription.
//  @param pin   @arg PIN
//  @param mode  @alias Trigger Mode

//#contract packet_sys_io_set_level_t
//  @alias       Set pin level
//  @description Drive an output pin high or low.
//  @param pin   @arg PIN
//  @param level @type bool

//#contract packet_sys_io_get_level_t
//  @alias       Read pin level
//  @param pin   @arg PIN
//  @returns     level

//#contract packet_sys_io_toggle_t
//  @alias       Toggle pin
//  @description Flip an output pin's current level.
//  @param pin   @arg PIN

//#contract packet_sys_io_get_voltage_t
//  @alias       Read pin voltage
//  @description Pin must be configured in ADC mode first.
//  @param pin   @arg PIN
//  @returns     voltage_mV

//#contract packet_sys_io_set_pwm_frequency_t
//  @alias       Set PWM frequency
//  @description Pin must be in PWM mode. Each pin has its own frequency, but pins share the PWM timers
//               by frequency: one timer per different frequency, any number of pins on each - up to 4
//               frequencies at once on the ESP32-S3, fewer if the firmware keeps timers for other
//               drivers. A frequency that needs a timer when none is free is refused and the pin keeps
//               its old one. Above ~19.5 kHz the duty has fewer than 4096 real steps
//               (steps = 80 MHz / frequency).
//  @param pin          @arg PIN
//  @param frequency_Hz @min 5 @max 5000000

//#contract packet_sys_io_set_pwm_duty_t
//  @alias       Set PWM duty
//  @description Pin must be in PWM mode. Before any frequency is set the pin runs at the default
//               frequency (1 kHz), which takes a PWM timer like any other frequency.
//  @param pin   @arg PIN
//  @param duty  @alias Duty @unit ticks @min 0 @max 4096 @note 4096 = always on.

#define HEADER_packet_sys_device_install_gpio_esp_t 0x40
typedef struct __packed {
  uint8_t device_id; //@max CONFIG_SYS_DEVICE_MAX_ID
} packet_sys_device_install_gpio_esp_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_install_gpio_esp_t(packet_sys_device_install_gpio_esp_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing gpio_esp (dev %u)", packet->device_id);
  d_gpio_esp_cfg_t cfg = {.device_id = packet->device_id};
  return d_gpio_esp_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_GPIO_ESP_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_gpio_esp_t, packet_sys_device_install_gpio_esp_t, decoder_packet_sys_device_install_gpio_esp_t)
