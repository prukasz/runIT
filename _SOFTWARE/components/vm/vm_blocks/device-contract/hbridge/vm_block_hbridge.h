#pragma once
#include "vm_block_device.h"

/*
 *            ---------------
 *  ->EN      |             | ->ENO
 *  (->CH)    |   HBRIDGE   | ->RESULT
 *  (->DRIVE) |             |
 *            ---------------
 *
 *  VM_BLK_HBRIDGE -- the HBRIDGE contract (SYS_HBRIDGE.MD): the channels of a motor driver. The operation is in the
 *  state (`op`, HBRIDGE_OP_*); its argument fields are the inputs, the first is the channel. The engine, the kinds
 *  of operation, the idle operation and ENO are in vm_block_device.h.
 *
 *  Operation (op)        Inputs                          Result   Kind
 *  SET_DRIVE             channel, drive in PERCENT       -        CHANGE
 *  BRAKE                 channel                         -        EDGE
 *  COAST                 channel                         -        EDGE
 *  GET_CURRENT           channel                         i32 mA   READ
 *
 *  Drive is a float, -100..100 % (a half bridge 0..100), from the input and from the constant alike: the block turns
 *  it into the contract's 12-bit steps (x40.95, rounded, saturated); the device range-checks. Idle: `idle_op` BRAKE or
 *  COAST (0 = hold what the bridge has) is sent once when the enable falls, for the channel last driven.
 */

//#block VM_BLK_HBRIDGE
//@alias H-bridge
//@category device
//@description Drives a motor channel of a motor driver, brakes or coasts it, and reads its current.
//@activation enabled
//@when Runs every pass while enabled: a drive is sent when it changes, brake and coast once on each rising enable, a reading on every pass.
//@face {op} {device_id}
//@contract HBRIDGE
//@data vm_block_device_data_t
/* Load-time check of the block's state and shape; the body, called every pass. */
bool vm_verify_hbridge(vm_block_h b);
void vm_blk_hbridge(vm_block_h b);
