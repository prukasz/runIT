#pragma once
#include "vm_block_device.h"

/*
 *            ----------------
 *  ->EN      |              | ->ENO
 *  (->VALUE) |    SERVO     |
 *            ----------------
 *
 *  VM_BLK_SERVO -- the servo device's own operations (device_servo, DEVICE_SERVO.MD). A servo has no contract, so this
 *  block is the servo's own: the same engine as the contract blocks (vm_block_device.h), the table of its ops.
 *  The operation is in the state (`op`, SERVO_OP_*); `device_id` is the servo.
 *
 *  Operation (op)        Input                                   Kind
 *  SET                   position (float)                        CHANGE
 *  HOME                  -                                       EDGE   (default_pos, plus the trim)
 *  RELEASE               -                                       EDGE   (stop the pulse: the servo goes limp)
 *  SET_TRIM              trim (degrees, or us if continuous)     CHANGE
 *
 *  No output: the block only commands the servo (the pulse it last sent is the device's GET_PULSE op, for the app).
 *
 *  Position is a float in the servo's unit: degrees for an angle servo, percent (-100..100) for a continuous one, or
 *  0..255 when the servo is in byte mode; the block sends tenths (x10, rounded) from the input and from the constant
 *  alike, and the device range-checks (an out of range value is an error, never clamped). Write-on-change: the servo
 *  is only commanded when the position changes. Idle: `idle_op` HOME (go to the default) or RELEASE (limp), 0 = hold
 *  where it is, sent once when the block is not enabled.
 */

/* Load-time check of the block's state and shape; the body, called every pass. */
bool vm_verify_servo(vm_block_h b);
void vm_blk_servo(vm_block_h b);
