#pragma once

#include "sys_device.h"
#include "sys_io.h"

//#device
//@alias DAC53202 dual DAC
//@description Two-channel I2C digital-to-analog converter: outputs a steady voltage on each channel.
//@protocol i2c
//@tags i2c dac voltage
//@datasheet https://www.ti.com/lit/ds/symlink/dac53202.pdf
//@pins IO @one-of [0..1] @alias DAC channel

#ifdef __cplusplus
extern "C" {
#endif

#define DAC53202_CHANNELS 2u
#define DAC53202_VREF_MV 3300  // both channels: VDD (3.3 V board supply) as reference, gain 1x

/**
 * @brief Create struct (packed, also the wire form):
 * CREATE = [0x05][id u16][op 0x0000][type u16 = SYS_DEVICE_TYPE_DAC53202][dac53202_cfg_t].
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;  //@alias I2C bus @description The I2C bus the chip is on @enum-ref sys_i2c_bus_e
  uint8_t i2c_addr;  //@alias I2C address @description The A0 pin selects one of four addresses @one-of [0x48..0x4B]
} dac53202_cfg_t;

#ifdef __cplusplus
}
#endif
