#pragma once

#include "sys_device.h"
#include "sys_io.h"
#include "sys_power_monitor.h"

//#device
//@alias INA3221 power monitor
//@description Three-channel I2C bus-voltage and shunt-current monitor with warning and critical alerts.
//@protocol i2c
//@tags i2c power voltage current monitor alert ic
//@datasheet https://www.ti.com/lit/ds/symlink/ina3221.pdf
//@pins POWER_MONITOR @one-of [0..2] @alias Monitor channel
//@event PMON_EV_OCP_CRITICAL PMON_EV_OCP_WARNING @channel Monitor channel

#ifdef __cplusplus
extern "C" {
#endif

#define INA3221_CHANNELS 3u
#define INA3221_I2C_ADDR_MIN 0x40u  // the A0 strap (GND / Vs+ / SDA / SCL) selects 0x40..0x43
#define INA3221_I2C_ADDR_MAX 0x43u

/**
 * @brief Create struct (packed, also the wire form):
 * CREATE = [0x05][id u16][op 0x0000][type u16 = SYS_DEVICE_TYPE_INA3221][ina3221_cfg_t].
 *
 * @warning An alert pin that is not wired MUST be spelled SYS_IO_PIN_NONE: a zero-filled pin is
 * device 0 / pin 0, a real pin. The pin's device must exist (sys_device_use) and can't be removed
 * while this one does.
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;  //@alias I2C bus @description The I2C bus the chip is on @enum-ref sys_i2c_bus_e
  uint8_t i2c_addr;  //@alias I2C address @description The A0 pin strap (GND, Vs+, SDA or SCL) selects one of four addresses @one-of [INA3221_I2C_ADDR_MIN..INA3221_I2C_ADDR_MAX]
  sys_io_pin_t crit_pin;  //@alias Critical alert @description Open-drain, active-low; needs CONFIGURE_INTR on its device @modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @default-mode $SYS_IO_MODE_INPUT_PULLUP
  sys_io_pin_t warn_pin;  //@alias Warning alert @description Open-drain, active-low; needs CONFIGURE_INTR on its device @modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @default-mode $SYS_IO_MODE_INPUT_PULLUP
  uint8_t inverted_mask;  //@alias Reversed shunts @description Bit N set: channel N's shunt is wired reversed, its current reads negative @min 0 @max 7 @default 0
} ina3221_cfg_t;

#ifdef __cplusplus
}
#endif
