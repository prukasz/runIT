#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_pca9685/include/device_pca9685.h"

//@id device_pca9685
//@version 1.0.0
//@title PCA9685 PWM expander
//@description Sixteen-channel I2C PWM expander with optional active-low output enable.
//@protocol i2c
//@tags i2c pwm gpio servo led ic
//@contract-provider $SYS_DEVICE_CONTRACT_IO
//@self-property PIN @one-of [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]

//@contract packet_sys_io_reset_t @alias Reset output
//@param pin @arg PIN @alias PWM Channel
//@description Set one PWM output duty to zero.

//@contract packet_sys_io_set_level_t @alias Set output level
//@param pin @arg PIN @alias PWM Channel
//@param level @alias Output Level @type bool
//@description Map a logical level to off or full PWM duty.

//@contract packet_sys_io_get_level_t @alias Read output level
//@param pin @arg PIN @alias PWM Channel
//@returns level @type bool

//@contract packet_sys_io_toggle_t @alias Toggle output
//@param pin @arg PIN @alias PWM Channel

//@contract packet_sys_io_set_pwm_duty_t @alias Set PWM duty
//@param pin @arg PIN @alias PWM Channel
//@param duty @alias Duty @type uint32_t @unit ticks @min 0 @max PCA9685_MAX_PWM_VALUE
//@description Duty values above 4095 are clamped by the adapter.

//@contract packet_sys_io_set_pwm_frequency_t @alias Set PWM frequency
//@param pin @arg PIN @alias PWM Channel
//@param frequency_Hz @alias PWM Frequency @type uint32_t @unit Hz
//@description Frequency is shared by all PCA9685 output channels.

#define HEADER_packet_sys_device_install_pca9685_t 0x41
typedef struct __packed {
  uint8_t device_id; //@required @min 0 @max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;   //@required @min 0 @max 1
  uint8_t i2c_addr;  //@required @min 0x40 @max 0x7F @note not range-checked by driver_pca9685.c - 7-bit address space, no software enforcement
  uint8_t oe_pin_device_id; //@group enable-output-pin @role device_id
  uint8_t oe_pin_pin;       //@group enable-output-pin @role pin @sentinel SYS_GPIO_NONE @note SYS_GPIO_NONE disables the output-enable pin; the pin is active-low
  uint8_t oe_pin_mode;      //@group enable-output-pin @role mode @enum-ref sys_io_mode_e
} packet_sys_device_install_pca9685_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_install_pca9685_t(packet_sys_device_install_pca9685_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing pca9685 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_pca9685_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                         .oe_pin = pin_ref_from_wire(packet->oe_pin_device_id, packet->oe_pin_pin, packet->oe_pin_mode)};
  return d_pca9685_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_PCA9685_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_pca9685_t, packet_sys_device_install_pca9685_t, decoder_packet_sys_device_install_pca9685_t)
