#pragma once
#include "vm_block_device.h"

/*
 *            ---------------
 *  ->EN      |             | ->ENO
 *  (->PIN)   |     IO      | ->RESULT
 *  (->ARG1)  |             |
 *            ---------------
 *
 *  VM_BLK_IO -- the IO contract (SYS_IO_CONTRACT.MD): the pins of a device (ESP GPIO, an expander, a PWM chip). The
 *  operation is in the state (`op`, IO_OP_*); its argument fields are the inputs, the first is the pin. The engine,
 *  the kinds of operation (CHANGE / EDGE / READ), the idle operation and ENO are in vm_block_device.h.
 *
 *  Operation (op)        Inputs               Result   Kind
 *  LEVEL write           pin, level (0 / 1)   -        CHANGE
 *  TOGGLE                pin                  -        EDGE
 *  LEVEL read            pin                  u8       READ
 *  PWM_DUTY              pin, duty (0..4095)  -        CHANGE
 *  PWM_FREQ              pin, Hz              -        CHANGE
 *  MODE                  pin, mode            -        CHANGE
 *  RESET                 pin                  -        EDGE
 *  VOLTAGE read          pin                  V        READ
 *  VOLTAGE write         pin, V               -        CHANGE
 *
 *  Not here: INTR (a setup struct), the pins' state (AVAILABLE, LOCKED, LOCK, UNLOCK) and the reads of many pins
 *  (LEVELS, VOLTAGES).
 */

/* Load-time check of the block's state and shape; the body, called every pass. */
bool vm_verify_io(vm_block_h b);
void vm_blk_io(vm_block_h b);
