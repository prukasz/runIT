#pragma once

#include "sys_power.h"
#include "vm_block_helpers.h"

/*
 *            ------------------
 *   ->EN     |                | ->ENO
 *            |  POWER_MONITOR | ->VOLTAGE
 *            |                | ->CURRENT
 *            ------------------
 *
 *  VM_BLK_POWER_MONITOR -- reads one channel of a power monitor device (INA3221 ...) through the
 *  POWER_MONITOR contract: sys_power_monitor_get_voltage() and sys_power_monitor_get_current().
 *
 *  Features:
 *  - Reads every pass while enabled; put a PERIODIC's ENO on EN to read at a fixed rate.
 *  - Voltage in volts, current in amperes (float; the contract answers in mV and mA, the sign is the device's).
 *  - Both outputs are optional: an unwired one is not read.
 *  - ENO is true when every wired output was written this pass.
 */

//@data vm_block_power_monitor_data_t
typedef struct __attribute__((aligned(4))) {
  uint8_t device_id;  // @description Power monitor device @id device @contract packet_sys_power_monitor_get_voltage_t
  uint8_t channel;    // @description Channel of the device to read @id pin @device-field device_id
  uint8_t _pad[2];    // @description Align to 4 bytes
} vm_block_power_monitor_data_t;

_Static_assert(sizeof(vm_block_power_monitor_data_t) == 4, "vm_block_power_monitor_data_t must be 4 bytes");
#define VM_POWER_MONITOR_CUSTOM_LEN sizeof(vm_block_power_monitor_data_t)

/* Pins, by index: VM_OUT_<BLOCK>_<PIN>. Titles and descriptions are in power_monitor.display.json. */
//#block-enum @alias Power Monitor Outputs
typedef enum vm_out_power_monitor_e {
  VM_OUT_POWER_MONITOR_VOLTAGE = 0,  //@out @value f32
  VM_OUT_POWER_MONITOR_CURRENT = 1,  //@out @value f32
} vm_out_power_monitor_e;

/* The state has nothing the pin shape does not cover: no load-time check. */
/* The body, called every pass.
   Face, titles and descriptions: power_monitor.display.json; the app's power_monitor.content.json is generated from this header. */
//#vm-block VM_BLK_POWER_MONITOR @id 17
//@activation enabled
void vm_blk_power_monitor(vm_block_h b);
