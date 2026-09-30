#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_pca9685/include/device_pca9685.h"

//#device device_pca9685
//  @title       PCA9685 PWM expander
//  @description Sixteen-channel I2C PWM expander with optional active-low output enable.
//  @protocol    i2c
//  @tags        i2c pwm gpio servo led ic
//  @datasheet   https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf
//  @contract-provider $SYS_DEVICE_CONTRACT_IO

//#self-property PIN
//  @one-of   [0..15]
//  @alias    PWM Channel

//#contract packet_sys_io_reset_t
//  @alias       Reset output
//  @description Set one PWM output duty to zero.
//  @param pin   @arg PIN

//#contract packet_sys_io_set_level_t
//  @alias       Set output level
//  @description Map a logical level to off or full PWM duty.
//  @param pin   @arg PIN
//  @param level @alias Output Level @type bool

//#contract packet_sys_io_get_level_t
//  @alias       Read output level
//  @param pin   @arg PIN
//  @returns     level

//#contract packet_sys_io_toggle_t
//  @alias       Toggle output
//  @param pin   @arg PIN

//#contract packet_sys_io_set_pwm_duty_t
//  @alias       Set PWM duty
//  @description Duty values above 4095 are clamped by the adapter.
//  @param pin   @arg PIN
//  @param duty  @alias Duty @unit ticks @min 0 @max PCA9685_MAX_PWM_VALUE

//#contract packet_sys_io_set_pwm_frequency_t
//  @alias       Set PWM frequency (all channels)
//  @description The PCA9685 has one PWM frequency for all 16 channels: setting it changes every channel.
//  @param pin          @device-wide
//  @param frequency_Hz @min PCA9685_MIN_FREQUENCY_HZ @max PCA9685_MAX_FREQUENCY_HZ @default 50

#define HEADER_packet_sys_device_install_pca9685_t 0x41
typedef struct __packed {
  uint8_t device_id;     //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;      //@alias I2C Address @one-of [0x40..0x6F, 0x71..0x77]
                         //  @note Address pins A0-A5 select the address; 0x70 is All Call and 0x78-0x7F are reserved.
  pin_ref_wire_t oe_pin; //@note Active-low.
} packet_sys_device_install_pca9685_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_install_pca9685_t(packet_sys_device_install_pca9685_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing pca9685 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_pca9685_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                         .oe_pin = pin_ref_from_wire(packet->oe_pin)};
  err_h err = PIN_REFS_BELOW(cfg.device_id, cfg.oe_pin);
  if (err) return err;
  return d_pca9685_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_PCA9685_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_pca9685_t, packet_sys_device_install_pca9685_t, decoder_packet_sys_device_install_pca9685_t)
