#pragma once

#include "sys_device.h"
#include "sys_io.h"
#include "sys_power_monitor.h"
#include "sys_power_usb_pd.h"
#include "sys_power_vreg.h"

//#device
//@alias AP33772S USB-C PD sink
//@description USB-C Power Delivery sink controller that negotiates voltage and current from a charger and can also act as a regulated, monitored output.
//@protocol i2c
//@tags i2c usb-c power-delivery power voltage current
//@datasheet https://www.diodes.com/assets/Datasheets/AP33772S.pdf
//@pins POWER_MONITOR @one-of [0] @alias Channel

#ifdef __cplusplus
extern "C" {
#endif

#define AP33772S_I2C_ADDR 0x52u  // fixed

/**
 * @brief Create struct (packed, also the wire form):
 * CREATE = [0x05][id u16][op 0x0000][type u16 = SYS_DEVICE_TYPE_AP33772S][ap33772s_cfg_t].
 *
 * @warning A pin that is not wired MUST be spelled SYS_IO_PIN_NONE: a zero-filled pin is device 0 / pin 0, a real
 * pin. The pin's device must exist (sys_device_use) and can't be removed while this one does.
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;  //@alias I2C bus @description The I2C bus the chip is on @enum-ref sys_i2c_bus_e
  uint8_t i2c_addr;  //@alias I2C address @description The chip's fixed address @one-of [AP33772S_I2C_ADDR]
  sys_io_pin_t intr_pin;  //@alias INT @description Active-low interrupt; needs CONFIGURE_INTR on its device. Leave unwired when not connected. @modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @default-mode $SYS_IO_MODE_INPUT_PULLUP
} ap33772s_cfg_t;

#ifdef __cplusplus
}
#endif
