#pragma once

#include "sys_device.h"
#include "sys_io.h"
#include "sys_power_vreg.h"

//#device
//@alias TPS55289 buck-boost regulator
//@description Adjustable I2C buck-boost voltage regulator: a programmable power output with a current limit and enable control.
//@protocol i2c
//@tags i2c power voltage current regulator
//@datasheet https://www.ti.com/lit/ds/symlink/tps55289.pdf
//@event VREG_EV_OVP VREG_EV_OCP VREG_EV_SCP VREG_EV_READY

#ifdef __cplusplus
extern "C" {
#endif

#define TPS55289_MIN_VOLTAGE_MV 3000u
#define TPS55289_MAX_VOLTAGE_MV 20000u
#define TPS55289_MIN_CURRENT_MA 200u
#define TPS55289_MAX_CURRENT_MA 5500u

#define TPS55289_I2C_ADDR_74 0x74u  // the MODE pin selects one of the two addresses
#define TPS55289_I2C_ADDR_75 0x75u

/* Own ops: how the chip protects itself (registers; SET while suspended: ERR_DEV_SUSPENDED, GET always). */
#define TPS55289_OP_SET_PROTECTION SYS_DEVICE_OWN_OP(0x01)  // args tps55289_protection_t
#define TPS55289_OP_GET_PROTECTION SYS_DEVICE_OWN_OP(0x02)  // -> tps55289_protection_t

#define TPS55289_OCP_DELAY_MAX 3u  // VOUT_SR OCP_DELAY: 0 = 128 us, 1 = 3.072 ms, 2 = 6.144 ms, 3 = 12.288 ms
#define TPS55289_FAULT_OVP 0x01u   // fault_mask bits: the fault pulls FB/INT low, so it becomes an event
#define TPS55289_FAULT_OCP 0x02u
#define TPS55289_FAULT_SCP 0x04u
#define TPS55289_FAULT_ALL 0x07u

/**
 * @brief The chip's own protection (datasheet 7.3.14-7.3.16, 7.6.3-7.6.6). Kept by the device and written
 * again each time EN goes high (the chip forgets its registers while EN is low). Default: 0, 1, ALL, 0
 * (the chip's reset values, every fault reported).
 */
typedef struct __attribute__((packed)) {
  uint8_t ocp_delay;  //@alias OCP delay @description How long the output may exceed the current limit before it is held @choices 0=128_us,1=3.072_ms,2=6.144_ms,3=12.288_ms
  uint8_t hiccup;  //@alias Hiccup @description On a short circuit the chip stops for 76 ms and soft-starts again (on) or stays in limit (off) @choices 0=Off,1=On
  uint8_t fault_mask;  //@alias Fault mask @description Which faults are indicated on FB/INT: bit 0 over-voltage, 1 over-current, 2 short circuit @min 0 @max 7
  uint8_t discharge;  //@alias Discharge @description Switching the output off discharges it (100 mA sink) before EN goes low @choices 0=Off,1=On
} tps55289_protection_t;

/**
 * @brief Create struct (packed, also the wire form):
 * CREATE = [0x05][id u16][op 0x0000][type u16 = SYS_DEVICE_TYPE_TPS55289][tps55289_cfg_t].
 *
 * @warning A pin that is not wired MUST be spelled SYS_IO_PIN_NONE: a
 * zero-filled pin is device 0 / pin 0, a real pin. The pin's device must
 * exist (sys_device_use: ERR_DEV_NOT_FOUND) and can't be removed while this one does.
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;  //@alias I2C bus @description The I2C bus the chip is on @enum-ref sys_i2c_bus_e
  uint8_t i2c_addr;  //@alias I2C address @description The MODE pin selects one of two addresses @one-of [TPS55289_I2C_ADDR_74, TPS55289_I2C_ADDR_75]
  sys_io_pin_t intr_pin;  //@alias FB/INT @description Open-drain, active-low: faults; needs CONFIGURE_INTR on its device @modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @default-mode $SYS_IO_MODE_INPUT_PULLUP
  sys_io_pin_t en_pin;  //@alias EN @description The hardware switch of the rail; the chip forgets its registers while it is low @default-mode $SYS_IO_MODE_OUTPUT_PUSH_PULL
} tps55289_cfg_t;

#ifdef __cplusplus
}
#endif
