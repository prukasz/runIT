#pragma once

#include "device_servo.h"
#include "vm_block_helpers.h"

/*
 *            ----------------
 *   ->EN     |              | ->ENO
 *  (->VALUE) |    SERVO     |
 *            ----------------
 *
 *  VM_BLK_SERVO -- moves a servo device (device_servo) to a position.
 *  The position is a float in the servo's own unit: degrees for an angle servo, percent (-100..100) for a
 *  continuous one, or 0..255 when the device steers by byte. The block sends it as tenths and the device checks
 *  the range (an out of range value is an error, never clamped).
 *
 *  Features:
 *  - Write-on-change: the servo is only commanded when the value (or the action) changes.
 *  - Block not enabled: what the servo does is set by when_not_active (HOLD / DEFAULT / RELEASE).
 *  - ENO is true while the servo follows the value this pass.
 *
 *  The wire ops are the device's (SERVO_OP_*); the block builds the router frame [op][device_id][args].
 */

//#block-enum @alias Servo Idle Action
typedef enum {
  VM_SERVO_IDLE_HOLD = 0,     // Leave the servo where it is (the default: 0 is what a fresh block holds)
  VM_SERVO_IDLE_DEFAULT = 1,  // Go to the device's default position
  VM_SERVO_IDLE_RELEASE = 2,  // Stop the pulse: the servo goes limp
} vm_servo_idle_e;

// What was last commanded; runtime only.
typedef enum {
  VM_SERVO_LAST_NONE = 0,
  VM_SERVO_LAST_VALUE = 1,
  VM_SERVO_LAST_DEFAULT = 2,
  VM_SERVO_LAST_RELEASE = 3,
} vm_servo_last_e;

//@data vm_block_servo_data_t
typedef struct __attribute__((aligned(4))) {
  float    value;            // @description Position when the Value input is unwired: degrees, percent, or 0..255 in byte mode
  uint8_t  device_id;        // @description Servo device @id device @contract device_servo
  uint8_t  when_not_active;  // @description What the servo does while the block is not enabled @enum-ref vm_servo_idle_e
  uint8_t  last;             // @description vm_servo_last_e, what was commanded last @runtime
  uint8_t  _pad;             // @description Align the next field
  int16_t  last_x10;         // @description Value last sent, in tenths @runtime
  uint8_t  _pad2[2];         // @description Align to 12 bytes
} vm_block_servo_data_t;

_Static_assert(sizeof(vm_block_servo_data_t) == 12, "vm_block_servo_data_t must be 12 bytes");
#define VM_SERVO_CUSTOM_LEN sizeof(vm_block_servo_data_t)

/* Pins, by index: VM_IN_<BLOCK>_<PIN>. Titles and descriptions are in servo.display.json. */
//#block-enum @alias Servo Inputs
typedef enum vm_in_servo_e {
  VM_IN_SERVO_VALUE = 0,  //@in @value f32 @overrides value @spec POSITION @device-field device_id
} vm_in_servo_e;

/* Load-time check of the block's state; the pin shape comes from the pin enum above. */
//@rule when_not_active is a vm_servo_idle_e value. @error ERR_VM_BLK_BAD_SHAPE
//@rule value is a finite number. @error ERR_VM_BLK_BAD_SHAPE
bool vm_verify_servo(vm_block_h b);
/* The body, called every pass.
   Face, titles and descriptions: servo.display.json; the app's servo.content.json is generated from this header. */
//#vm-block VM_BLK_SERVO @id 16
//@activation enabled
void vm_blk_servo(vm_block_h b);
