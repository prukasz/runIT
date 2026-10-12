#pragma once
#include "vm_block_device.h"

/*
 *            ---------------
 *  ->EN      |             | ->ENO
 *  (->ARG0)  |  TPS55289   | ->OCP_DELAY
 *  (->ARG1)  |             | ->HICCUP
 *  (->ARG2)  |             | ->FAULT_MASK
 *  (->ARG3)  |             | ->DISCHARGE
 *            ---------------
 *
 *  VM_BLK_TPS55289 -- the TPS55289 regulator's own operations (device_tps55289, DEVICE_TPS55289.MD): how the chip
 *  protects itself. Voltage, current limit and enable are the POWER_VREG block; this one is what only this chip has.
 *  Same engine as the contract blocks (vm_block_device.h), the table of its ops. The operation is in the state
 *  (`op`, TPS55289_OP_*); `device_id` is the regulator.
 *
 *  Operation (op)        Inputs                                                Outputs                          Kind
 *  SET_PROTECTION        ocp_delay (0..3), hiccup (0 / 1),                     -                                CHANGE
 *                        fault_mask (TPS55289_FAULT_*), discharge (0 / 1)
 *  GET_PROTECTION        -                                                     0 ocp_delay, 1 hiccup,           READ
 *                                                                              2 fault_mask, 3 discharge
 *
 *  The device keeps the values and writes them again each time EN goes high. Usually this is install-time setup
 *  (the device's record); the block is for a program that changes the protection while it runs.
 */

//#block VM_BLK_TPS55289
//@alias TPS55289
//@category device
//@description The regulator's own protection settings.
//@activation enabled
//@when Runs every pass while enabled: the protection is sent when it changes, a reading on every pass.
//@face {op} {device_id}
//@device-type device_tps55289
//@data vm_block_device_data_t
/* Load-time check of the block's state and shape; the body, called every pass. */
bool vm_verify_tps55289(vm_block_h b);
void vm_blk_tps55289(vm_block_h b);
