#pragma once
#include "vm_block_device.h"

/*
 *            ----------------
 *  ->EN      |              | ->ENO
 *  (->CH)    | POWER_MONITOR| ->RESULT
 *  (->ARG1)  |              |
 *            ----------------
 *
 *  VM_BLK_POWER_MONITOR -- the POWER_MONITOR contract (SYS_POWER.MD): the measured channels of a monitor (INA3221).
 *  The operation is in the state (`op`, PMON_OP_*); its argument fields are the inputs, the first is the channel. The
 *  engine, the kinds of operation and ENO are in vm_block_device.h.
 *
 *  Operation (op)        Inputs                          Result   Kind
 *  GET_VOLTAGE           channel                         i32 mV   READ
 *  GET_CURRENT           channel                         i32 mA   READ
 *  GET_POWER             channel                         i32 mW   READ
 *  SET_ALERT             channel, alert, threshold mA    -        CHANGE
 *
 *  A reading is asked on every enabled pass: gate it with PERIODIC to ask less often.
 */

//#block VM_BLK_POWER_MONITOR
//@alias Power monitor
//@category device
//@description Reads the voltage, current or power of a measured channel and arms its current alerts.
//@activation enabled
//@when Runs every pass while enabled: a reading on every pass (gate it with Every to read less often), an alert setting when it changes.
//@face {op} {device_id}
//@contract POWER_MONITOR
//@data vm_block_device_data_t
/* Load-time check of the block's state and shape; the body, called every pass. */
bool vm_verify_power_monitor(vm_block_h b);
void vm_blk_power_monitor(vm_block_h b);
