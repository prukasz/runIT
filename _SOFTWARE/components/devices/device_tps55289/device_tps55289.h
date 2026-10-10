#pragma once

#include "sys_device.h"
#include "sys_io.h"
#include "sys_power_vreg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TPS55289_MIN_VOLTAGE_MV 3000u
#define TPS55289_MAX_VOLTAGE_MV 20000u
#define TPS55289_MIN_CURRENT_MA 200u
#define TPS55289_MAX_CURRENT_MA 5500u

#define TPS55289_I2C_ADDR_74 0x74u  // the MODE pin selects one of the two addresses
#define TPS55289_I2C_ADDR_75 0x75u

/**
 * @brief Create struct (packed, also the wire form):
 * CREATE = [0x05][id u16][op 0x0000][type u16 = SYS_DEVICE_TYPE_TPS55289][tps55289_cfg_t].
 *
 * @warning A pin that is not wired MUST be spelled SYS_IO_PIN_NONE: a
 * zero-filled pin is device 0 / pin 0, a real pin. The pin's device must
 * exist (sys_device_use: ERR_DEV_NOT_FOUND) and can't be removed while this one does.
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;        // sys_i2c_bus_e
  uint8_t i2c_addr;       // TPS55289_I2C_ADDR_74 / _75
  sys_io_pin_t intr_pin;  // FB/INT, open drain, active low: faults (needs CONFIGURE_INTR on its device)
  sys_io_pin_t en_pin;    // EN: the hardware switch of the rail; the chip forgets its registers while it is low
} tps55289_cfg_t;

#ifdef __cplusplus
}
#endif
