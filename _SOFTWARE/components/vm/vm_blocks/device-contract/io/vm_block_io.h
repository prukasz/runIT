#pragma once
#include "vm_block_device.h"

/*
 *            ---------------
 *  ->EN      |             | ->ENO
 *  (->PIN)   |     IO      | ->RESULT
 *  (->ARG1)  |             |
 *            ---------------
 *
 *  VM_BLK_IO -- the IO contract (SYS_IO.MD): the pins of a device (ESP GPIO, an expander, a PWM chip). The
 *  operation is in the state (`op`, IO_OP_*); its argument fields are the inputs, the first is the pin. The engine,
 *  the kinds of operation (CHANGE / EDGE / READ), the idle operation and ENO are in vm_block_device.h.
 *
 *  Operation (op)        Inputs               Result   Kind
 *  SET_LEVEL             pin, level (0 / 1)   -        CHANGE
 *  TOGGLE                pin                  -        EDGE
 *  GET_LEVEL             pin                  u8       READ
 *  SET_PWM_DUTY          pin, duty (0..4095)  -        CHANGE
 *  SET_PWM_FREQ          pin, Hz              -        CHANGE
 *  SET_MODE              pin, mode            -        CHANGE
 *  RESET                 pin                  -        EDGE
 *  GET_VOLTAGE           pin                  i32 mV   READ
 *  SET_VOLTAGE           pin, mV              -        CHANGE
 *
 *  Not here: CONFIGURE_INTR (a setup struct) and the PINS contract (install time).
 */

//#block VM_BLK_IO
//@alias IO pin
//@category device
//@description Drives or reads a pin of a device: a level, a PWM duty or frequency, a voltage.
//@activation enabled
//@when Runs every pass while enabled: a setting is sent when it changes, an action once on each rising enable, a reading on every pass.
//@face {op} {device_id}
//@contract IO
//@data vm_block_device_data_t
/* Load-time check of the block's state and shape; the body, called every pass. */
bool vm_verify_io(vm_block_h b);
void vm_blk_io(vm_block_h b);
