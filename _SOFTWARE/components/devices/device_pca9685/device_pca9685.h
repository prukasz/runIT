#pragma once

#include "sys_device.h"
#include "sys_io.h"

//#device
//@alias PCA9685 PWM expander
//@description Sixteen-channel I2C PWM expander with optional active-low output enable.
//@protocol i2c
//@tags i2c pwm gpio servo led ic
//@datasheet https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf
//@pins IO @one-of [0..15] @alias PWM channel
//@param IO_OP_SET_PWM_FREQ.pin @device-wide @note One frequency for all channels
//@param IO_OP_SET_PWM_FREQ.frequency_Hz @min PCA9685_MIN_FREQUENCY_HZ @max PCA9685_MAX_FREQUENCY_HZ @default 50

#ifdef __cplusplus
extern "C" {
#endif

#define PCA9685_CHANNELS 16u
#define PCA9685_MIN_FREQUENCY_HZ 24u    // prescaler 255 at the 25 MHz oscillator
#define PCA9685_MAX_FREQUENCY_HZ 1526u  // prescaler 3

/**
 * @brief Create struct (packed, also the wire form):
 * CREATE = [0x05][id u16][op 0x0000][type u16 = SYS_DEVICE_TYPE_PCA9685][pca9685_cfg_t].
 *
 * @warning A board without OE control MUST spell it `.oe_pin = SYS_IO_PIN_NONE`:
 * a zero-filled pin is device 0 / pin 0, a real pin. The pin's device must exist
 * (sys_device_use: ERR_DEV_NOT_FOUND) and can't be removed while this one does.
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;  //@alias I2C bus @description The I2C bus the chip is on @enum-ref sys_i2c_bus_e
  uint8_t i2c_addr;  //@alias I2C address @description Address pins A0-A5 select the address; 0x70 is All Call and 0x78 to 0x7F are reserved @one-of [0x40..0x6F, 0x71..0x77]
  sys_io_pin_t oe_pin;  //@alias Output enable @description Active-low; leave unwired when the board has no OE control @default-mode $SYS_IO_MODE_OUTPUT_PUSH_PULL
} pca9685_cfg_t;

#ifdef __cplusplus
}
#endif
