#pragma once
#include "vm_block_device.h"

/*
 *            ----------------
 *  ->EN      |              | ->ENO
 *  (->SPEED) |   DC MOTOR   | (->RESULT)
 *            ----------------
 *
 *  VM_BLK_DC_MOTOR -- the DC motor device's own operations (device_dc_motor). The motor's behaviour (ramps, dead zone,
 *  start and top speed, trim, reversing through stop, stall) is the device's: the block only tells it what to do. The
 *  same engine as the contract blocks (vm_block_device.h), the table of the motor's ops; `device_id` is the motor.
 *
 *  Operation (op)   Input / output                         Kind
 *  SPEED            speed in % (float, -100..100)          CHANGE  (tenths on the wire; past +-100 is held to it)
 *  STOP             -                                      EDGE    (to stop along the ramp)
 *  BRAKE            -                                      EDGE    (at once)
 *  COAST            -                                      EDGE    (at once)
 *  OUTPUT           -> the speed it runs at now, %         READ
 *  CURRENT          -> mA                                  READ
 *  STALLED          -> 1 = a stall stopped it              READ
 *
 *  Idle: `idle_op` STOP, BRAKE or COAST (0 = keep running), sent once when the block is not enabled.
 */

/* Load-time check of the block's state and shape; the body, called every pass. */
bool vm_verify_dc_motor(vm_block_h b);
void vm_blk_dc_motor(vm_block_h b);
