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

/* Own ops: how the chip protects itself (registers; the write while suspended: ERR_DEV_SUSPENDED, the read always). */
#define TPS55289_OP_PROTECTION SYS_DEVICE_OWN_OP(0x01)  // read -> tps55289_protection_t, write tps55289_protection_t

#define TPS55289_OCP_DELAY_MAX 3u  // VOUT_SR OCP_DELAY: 0 = 128 us, 1 = 3.072 ms, 2 = 6.144 ms, 3 = 12.288 ms
#define TPS55289_FAULT_OVP     0x01u   // fault_mask bits: the fault pulls FB/INT low, so it becomes an event
#define TPS55289_FAULT_OCP     0x02u
#define TPS55289_FAULT_SCP     0x04u
#define TPS55289_FAULT_ALL     0x07u

/**
 * @brief Chip-level protection register settings.
 */
typedef struct __attribute__((packed)) {
  uint8_t ocp_delay;   /**< OCP delay index (0=128us, 1=3.072ms, 2=6.144ms, 3=12.288ms) */
  uint8_t hiccup;      /**< 1 = restart on short circuit after 76ms, 0 = latch off */
  uint8_t fault_mask;  /**< Fault mask: bit 0 OVP, bit 1 OCP, bit 2 SCP */
  uint8_t discharge;   /**< 1 = active discharge on disable */
} tps55289_protection_t;

/**
 * @brief TPS55289 configuration struct (wire format for CREATE).
 *
 * If a pin is not wired, set it to SYS_IO_PIN_NONE.
 */
typedef struct __attribute__((packed)) {
  uint8_t i2c_bus;        /**< I2C bus index */
  uint8_t i2c_addr;       /**< 7-bit I2C address (0x74 or 0x75) */
  sys_io_pin_t intr_pin;  /**< Active-low FB/INT interrupt pin */
  sys_io_pin_t en_pin;    /**< Hardware enable pin */
} tps55289_cfg_t;

#ifdef __cplusplus
}
#endif
