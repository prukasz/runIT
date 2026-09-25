#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_gpio_esp/include/device_gpio_esp.h"

//@id device_gpio_esp
//@version 1.0.0
//@title ESP32 native GPIO
//@description The board's own onboard GPIO pins - digital input/output, interrupts, ADC voltage reads and PWM outputs. Always available; nothing to install on the wire beyond a device ID.
//@protocol native
//@tags gpio io adc voltage pwm
//@contract-provider $SYS_DEVICE_CONTRACT_IO
//@property PIN-MODE @enum-ref sys_io_mode_e @one-of [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP, $SYS_IO_MODE_INPUT_PULLDOWN, $SYS_IO_MODE_OUTPUT_PUSH_PULL, $SYS_IO_MODE_OUTPUT_OPEN_DRAIN, $SYS_IO_MODE_OUTPUT_OPEN_DRAIN_PULLUP, $SYS_IO_MODE_PWM, $SYS_IO_MODE_ADC]

//@contract packet_sys_io_reset_t @alias Reset pin
//@param pin @alias GPIO Pin @note Valid pin numbers depend on the specific ESP32 variant/board - not a fixed set
//@description Releases the pin back to its unconfigured state.

//@contract packet_sys_io_set_mode_t @alias Configure pin mode
//@param pin @alias GPIO Pin @note Valid pin numbers depend on the specific ESP32 variant/board - not a fixed set
//@param mode @arg PIN-MODE @alias Pin Mode
//@description DAC mode isn't available on native GPIO - use a DAC53202 device. PWM takes one PWM channel per pin: up to 8 pins at once on the ESP32-S3, fewer if the firmware keeps channels for other drivers.

//@contract packet_sys_io_configure_intr_t @alias Configure pin interrupt
//@param pin @alias GPIO Pin @note Valid pin numbers depend on the specific ESP32 variant/board - not a fixed set
//@param mode @alias Trigger Mode @enum-ref sys_io_intr_mode_e
//@param debounce @alias Debounce @type uint8_t @optional @default 0 @note 1 = ignore switch bounce
//@description Digital edge-triggered interrupt (not the ADC window comparator, which native GPIO doesn't have). Link it to the program or an action with an event subscription.

//@contract packet_sys_io_set_level_t @alias Set pin level
//@param pin @alias GPIO Pin @note Valid pin numbers depend on the specific ESP32 variant/board - not a fixed set
//@param level @alias Level @type bool
//@description Drive an output pin high or low.

//@contract packet_sys_io_get_level_t @alias Read pin level
//@param pin @alias GPIO Pin @note Valid pin numbers depend on the specific ESP32 variant/board - not a fixed set
//@returns level @type bool

//@contract packet_sys_io_toggle_t @alias Toggle pin
//@param pin @alias GPIO Pin @note Valid pin numbers depend on the specific ESP32 variant/board - not a fixed set
//@description Flip an output pin's current level.

//@contract packet_sys_io_get_voltage_t @alias Read pin voltage
//@param pin @alias GPIO Pin @note Valid pin numbers depend on the specific ESP32 variant/board - not a fixed set
//@returns voltage_mV @type uint32_t @unit mV
//@description Pin must be configured in ADC mode first.

//@contract packet_sys_io_set_pwm_frequency_t @alias Set PWM frequency
//@param pin @alias GPIO Pin @note Valid pin numbers depend on the specific ESP32 variant/board - not a fixed set
//@param frequency_Hz @alias PWM Frequency @type uint32_t @unit Hz @min 5 @max 5000000
//@description Pin must be in PWM mode. Each pin has its own frequency, but pins share the PWM timers by frequency: one timer per different frequency, any number of pins on each - up to 4 frequencies at once on the ESP32-S3, fewer if the firmware keeps timers for other drivers. A frequency that needs a timer when none is free is refused and the pin keeps its old one. Above ~19.5 kHz the duty has fewer than 4096 real steps (steps = 80 MHz / frequency).

//@contract packet_sys_io_set_pwm_duty_t @alias Set PWM duty
//@param pin @alias GPIO Pin @note Valid pin numbers depend on the specific ESP32 variant/board - not a fixed set
//@param duty @alias Duty @type uint32_t @unit ticks @min 0 @max 4096 @note 4096 = always on
//@description Pin must be in PWM mode. Before any frequency is set the pin runs at the default frequency (1 kHz), which takes a PWM timer like any other frequency.

#define HEADER_packet_sys_device_install_gpio_esp_t 0x40
typedef struct __packed {
  uint8_t device_id; //@required @min 0 @max CONFIG_SYS_DEVICE_MAX_ID
} packet_sys_device_install_gpio_esp_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_install_gpio_esp_t(packet_sys_device_install_gpio_esp_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing gpio_esp (dev %u)", packet->device_id);
  d_gpio_esp_cfg_t cfg = {.device_id = packet->device_id};
  return d_gpio_esp_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_GPIO_ESP_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_gpio_esp_t, packet_sys_device_install_gpio_esp_t, decoder_packet_sys_device_install_gpio_esp_t)
