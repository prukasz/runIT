#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_pca9685/include/device_pca9685.h"

//@id device_pca9685
//@version 1.0.0
//@title PCA9685 PWM expander
//@description Sixteen-channel I2C PWM expander with optional active-low output enable.
//@protocol i2c
//@tags i2c pwm gpio servo led ic
//@datasheet https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf
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

//@contract packet_sys_io_set_pwm_frequency_t @alias Set PWM frequency (all channels)
//@param pin @device-wide
//@param frequency_Hz @alias PWM Frequency @type uint32_t @unit Hz @min PCA9685_MIN_FREQUENCY_HZ @max PCA9685_MAX_FREQUENCY_HZ @default 50
//@description The PCA9685 has one PWM frequency for all 16 channels: setting it changes every channel.

#define HEADER_packet_sys_device_install_pca9685_t 0x41
typedef struct __packed {
  uint8_t device_id; //@required @min 0 @max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;   //@required @min 0 @max 1
  uint8_t i2c_addr;  //@required @one-of [0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x5B,0x5C,0x5D,0x5E,0x5F,0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x71,0x72,0x73,0x74,0x75,0x76,0x77] @note Address pins A0-A5 select the address; 0x70 is All Call and 0x78-0x7F are reserved
  uint8_t oe_pin_device_id; //@group enable-output-pin @role device_id
  uint8_t oe_pin_pin;       //@group enable-output-pin @role pin @sentinel SYS_GPIO_NONE @note SYS_GPIO_NONE disables the output-enable pin; the pin is active-low
  uint8_t oe_pin_mode;      //@group enable-output-pin @role mode @enum-ref sys_io_mode_e
} packet_sys_device_install_pca9685_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_install_pca9685_t(packet_sys_device_install_pca9685_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing pca9685 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_pca9685_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                         .oe_pin = pin_ref_from_wire(packet->oe_pin_device_id, packet->oe_pin_pin, packet->oe_pin_mode)};
  err_h err = PIN_REFS_BELOW(cfg.device_id, cfg.oe_pin);
  if (err) return err;
  return d_pca9685_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_PCA9685_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_pca9685_t, packet_sys_device_install_pca9685_t, decoder_packet_sys_device_install_pca9685_t)
