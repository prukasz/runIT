#pragma once
#include "vm_block_device.h"

/*
 *             ---------------
 *  ->EN       |             | ->ENO
 *  (->ARG0)   | POWER_VREG  |
 *             ---------------
 *
 *  VM_BLK_POWER_VREG -- the POWER_VREG contract (SYS_POWER.MD): an adjustable regulator with one output (TPS55289 rail).
 *  The operation is in the state (`op`, VREG_OP_*); its single argument is input 0. The engine, the kinds of
 *  operation, the idle operation and ENO are in vm_block_device.h.
 *
 *  Operation (op)        Input                  Kind
 *  SET_ENABLE            enable (0 / 1)         CHANGE
 *  SET_VOLTAGE           wanted voltage, mV     CHANGE
 *  SET_CURRENT           current limit, mA      CHANGE
 *
 *  The regulator keeps voltage and limit while the output is off. The power manager guards these calls like the
 *  remote ones (budget): a refusal is the call's error. SET_ENABLE and a higher SET_VOLTAGE return at once: wait for
 *  the contract's READY event (ON_EVENT) before using the rail. Idle: `idle_op` SET_ENABLE with `idle_k[0]` 0 switches
 *  the output off when the block is not enabled.
 */

//#block VM_BLK_POWER_VREG
//@alias Regulator
//@category device
//@description Switches an adjustable regulator on or off and sets its output voltage and current limit.
//@activation enabled
//@when Runs every pass while enabled; a setting is sent on the first pass and whenever it changes.
//@face {op} {device_id}
//@contract POWER_VREG
//@data vm_block_device_data_t
/* Load-time check of the block's state and shape; the body, called every pass. */
bool vm_verify_power_vreg(vm_block_h b);
void vm_blk_power_vreg(vm_block_h b);
