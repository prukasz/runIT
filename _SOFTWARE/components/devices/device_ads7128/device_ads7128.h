#pragma once

#include "sys_device.h"
#include "sys_io.h"

//#device
//@alias ADS7128 ADC
//@description Eight-channel ADC with I2C control and an on-chip window comparator.
//@protocol i2c
//@tags i2c adc io voltage ic
//@datasheet https://www.ti.com/lit/ds/symlink/ads7128.pdf
//@pins IO @one-of [0..7] @alias ADC channel
//@param IO_OP_SET_MODE.mode @one-of [$SYS_IO_MODE_ADC]
//@event IO_EV_ADC_OUTSIDE IO_EV_ADC_INSIDE @channel ADC channel

#ifdef __cplusplus
extern "C" {
#endif

#define ADS7128_CHANNELS 8u

/**
 * @brief Create struct (packed, also the wire form):
 * CREATE = [0x05][id u16][op 0x0000][type u16 = SYS_DEVICE_TYPE_ADS7128][ads7128_cfg_t].
 *
 * @warning A board without the ALERT pin wired MUST spell it `.intr_pin = SYS_IO_PIN_NONE`: a zero-filled pin is
 * device 0 / pin 0, a real pin. The pin's device must exist (sys_device_use: ERR_DEV_NOT_FOUND) and can't be removed
 * while this one does.
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;  //@alias I2C bus @description The I2C bus the chip is on @enum-ref sys_i2c_bus_e
  uint8_t i2c_addr;  //@alias I2C address @description The resistor on the ADDR pin selects the address at power-up @one-of [0x10..0x17]
  sys_io_pin_t intr_pin;  //@alias ALERT @description Open-drain, active-low; the device reads its falling edge @modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @default-mode $SYS_IO_MODE_INPUT_PULLUP
  uint16_t full_scale_mV;  //@alias ADC reference voltage @description The input voltage that reads full scale: the chip's AVDD on a bare chip (2350 to 5500 mV), 20000 on the runIT board (its front end scales the inputs to 20 V) @unit mV @min 1 @default 20000
} ads7128_cfg_t;

#ifdef __cplusplus
}
#endif
