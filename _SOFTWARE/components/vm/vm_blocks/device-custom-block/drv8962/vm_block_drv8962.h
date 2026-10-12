#pragma once
#include "vm_block_device.h"

/*
 *            ---------------
 *  ->EN      |             | ->ENO
 *  (->CH)    |   DRV8962   | ->FAULT
 *  (->ARG1)  |             | ->REASON
 *            ---------------
 *
 *  VM_BLK_DRV8962 -- the DRV8962 motor driver's own operations (device_drv8962, DEVICE_DRV8962.MD). Driving the
 *  channels is the HBRIDGE block; this one is what only this chip has. Same engine as the contract blocks
 *  (vm_block_device.h), the table of its ops. The operation is in the state (`op`, DRV8962_OP_*); `device_id` is the
 *  driver; the first input is the channel.
 *
 *  Operation (op)        Inputs                    Outputs                          Kind
 *  SET_CURRENT_LIMIT     channel, limit mA (0 = none)   -                           CHANGE
 *  CLEAR_FAULT           channel                   -                                EDGE   (nSLEEP pulse; the channel brakes)
 *  GET_FAULT             channel                   0: fault (0 / 1), 1: reason      READ
 *
 *  The reason is sys_hbridge_fault_reason_e (1 overcurrent, 2 thermal / undervoltage, 3 external). Faults are also
 *  events of the HBRIDGE contract (NOTIFY): an ON_EVENT block reacts to them without asking.
 */

//#block VM_BLK_DRV8962
//@alias DRV8962
//@category device
//@description The motor driver's own settings: the current limit of a channel and its fault latch.
//@activation enabled
//@when Runs every pass while enabled: a limit is sent when it changes, clear once on each rising enable, a reading on every pass.
//@face {op} {device_id}
//@device-type device_drv8962
//@data vm_block_device_data_t
/* Load-time check of the block's state and shape; the body, called every pass. */
bool vm_verify_drv8962(vm_block_h b);
void vm_blk_drv8962(vm_block_h b);
