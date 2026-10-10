#pragma once

#include "sys_device.h"
#include "sys_io.h"

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
  uint8_t i2c_bus;     // sys_i2c_bus_e
  uint8_t i2c_addr;    // 7-bit
  sys_io_pin_t oe_pin;  // output enable, active low; the device drives it through the IO contract
} pca9685_cfg_t;

#ifdef __cplusplus
}
#endif
