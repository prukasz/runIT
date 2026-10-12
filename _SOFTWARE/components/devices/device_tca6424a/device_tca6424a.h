#pragma once

#include "sys_device.h"
#include "sys_io.h"

//#device
//@alias TCA6424A GPIO expander
//@description 24-bit I2C GPIO expander: adds extra digital input/output pins over I2C.
//@protocol i2c
//@tags i2c gpio io expander
//@datasheet https://www.ti.com/lit/ds/symlink/tca6424a.pdf
//@pins IO @one-of [0..23] @alias Expander pin
//@event IO_EV_RISING IO_EV_FALLING @channel Expander pin

#ifdef __cplusplus
extern "C" {
#endif

#define TCA6424A_PINS 24u
#define TCA6424A_I2C_ADDR_LOW 0x22u   // ADDR low
#define TCA6424A_I2C_ADDR_HIGH 0x23u  // ADDR high

/**
 * @brief Create struct (packed, also the wire form):
 * CREATE = [0x05][id u16][op 0x0000][type u16 = SYS_DEVICE_TYPE_TCA6424A][tca6424a_cfg_t].
 *
 * @warning A pin that is not wired MUST be spelled SYS_IO_PIN_NONE: a zero-filled pin is device 0 / pin 0, a real
 * pin. The pins' devices must exist (sys_device_use: ERR_DEV_NOT_FOUND) and can't be removed while this one does.
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;  //@alias I2C bus @description The I2C bus the chip is on @enum-ref sys_i2c_bus_e
  uint8_t i2c_addr;  //@alias I2C address @description ADDR low selects 0x22, high 0x23 @one-of [TCA6424A_I2C_ADDR_LOW, TCA6424A_I2C_ADDR_HIGH]
  sys_io_pin_t intr_pin;  //@alias INT @description Open-drain, active-low: an input changed; needs CONFIGURE_INTR on its device @modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @default-mode $SYS_IO_MODE_INPUT_PULLUP
  sys_io_pin_t rst_pin;  //@alias RESET @description Active-low; suspending the device holds the chip in reset @default-mode $SYS_IO_MODE_OUTPUT_PUSH_PULL
} tca6424a_cfg_t;

#ifdef __cplusplus
}
#endif
