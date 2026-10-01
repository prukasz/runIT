#pragma once
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"

#define PCA9685_TYPE_ID 0x41  /* the byte after 0x00 in a create frame: [0x00][0x41][d_pca9685_cfg_t] */
#define PCA9685_MAX_PWM_VALUE 4095
#define PCA9685_MIN_FREQUENCY_HZ 24    // prescaler 255 at the 25 MHz oscillator
#define PCA9685_MAX_FREQUENCY_HZ 1526  // prescaler 3


// The annotations below describe the device to the app (data-structures/devices/*.generated.json,
// grammar: data-structures/auto-annotations/device/device-annotations.md).

//#device device_pca9685
//  @title       PCA9685 PWM expander
//  @description Sixteen-channel I2C PWM expander with optional active-low output enable.
//  @protocol    i2c
//  @tags        i2c pwm gpio servo led ic
//  @datasheet   https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf
//  @type-id     PCA9685_TYPE_ID
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
//  @description Duty values above 4095 are clamped by the device.
//  @param pin   @arg PIN
//  @param duty  @alias Duty @unit ticks @min 0 @max PCA9685_MAX_PWM_VALUE

//#contract packet_sys_io_set_pwm_frequency_t
//  @alias       Set PWM frequency (all channels)
//  @description The PCA9685 has one PWM frequency for all 16 channels: setting it changes every channel.
//  @param pin          @device-wide
//  @param frequency_Hz @min PCA9685_MIN_FREQUENCY_HZ @max PCA9685_MAX_FREQUENCY_HZ @default 50

/**
 * @brief PCA9685 PWM expander configuration. This is also the wire struct of the
 * create frame (packed, device_id first): the router hands the received bytes to
 * sys_device_create(&g_pca9685_class, ...) as they are, and the generator reads the field annotations from here.
 *
 * @warning A board without OE control MUST spell it `.oe_pin = SYS_IO_PIN_NONE_INIT`.
 *          Omitting the field zero-fills it to device 0 / pin 0, which is a real
 *          pin on a real device - not "unused".
 */
typedef struct __packed {
  uint8_t device_id;     //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;      //@alias I2C Address @one-of [0x40..0x6F, 0x71..0x77]
                         //  @note Address pins A0-A5 select the address; 0x70 is All Call and 0x78-0x7F are reserved.
  sys_io_pin_ref_t oe_pin; //@note Active-low.
} d_pca9685_cfg_t;

/**
 * @brief The PCA9685 device class: register it with sys_device_register_class(), create a
 * device with SYS_DEVICE_CREATE(&g_pca9685_class, &(d_pca9685_cfg_t){...}). The cfg is only
 * read during the call, so it may be a compound literal; ERR_DEV_PIN_ORDER if the OE pin
 * isn't on a lower-ID device.
 */
extern const sys_device_class_t g_pca9685_class;
