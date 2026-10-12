#pragma once
#include "vm_block_device.h"

/*
 *             ---------------
 *  ->EN       |             | ->ENO
 *  (->SOURCE) | POWER_SUPPLY| ->RESULT
 *             ---------------
 *
 *  VM_BLK_POWER_SUPPLY -- the POWER_SUPPLY contract (SYS_POWER.MD): a power mux, two switched sources into one load.
 *  At most one source is on; a change switches the old one off first and the new one on after the device's delay.
 *  The operation is in the state (`op`, SUPPLY_OP_*). The engine, the kinds of operation, the idle operation and
 *  ENO are in vm_block_device.h.
 *
 *  Operation (op)        Input                              Result            Kind
 *  SELECT                source: 0 off, 1 A, 2 B            -                 CHANGE
 *  GET_SOURCE            -                                  u8 source on      READ
 *
 *  Idle: `idle_op` SELECT with `idle_k[0]` 0 switches the load off when the block is not enabled.
 */

//#block VM_BLK_POWER_SUPPLY
//@alias Power mux
//@category device
//@description Selects which of two sources feeds the load and reads which one is on.
//@activation enabled
//@when Runs every pass while enabled; a selection is sent on the first pass and whenever it changes.
//@face {op} {device_id}
//@contract POWER_SUPPLY
//@data vm_block_device_data_t
/* Load-time check of the block's state and shape; the body, called every pass. */
bool vm_verify_power_supply(vm_block_h b);
void vm_blk_power_supply(vm_block_h b);
