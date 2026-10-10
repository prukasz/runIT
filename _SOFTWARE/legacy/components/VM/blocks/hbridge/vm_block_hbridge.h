#pragma once

#include "sys_hbridge.h"
#include "vm_block_helpers.h"

/*
 *            ------------------
 *   ->EN     |                | ->ENO
 *  (->DRIVE) |    HBRIDGE     |
 *            ------------------
 *
 *  VM_BLK_HBRIDGE -- drives one channel of an H-bridge device (DRV8962 ...) through the HBRIDGE contract:
 *  sys_hbridge_set_drive(), sys_hbridge_brake() and sys_hbridge_coast().
 *
 *  Features:
 *  - action selects what the block does while it is enabled: DRIVE follows the Drive input (percent, -100..100,
 *    the sign is the direction), BRAKE shorts the motor terminals, COAST releases them.
 *  - Write-on-change: the bridge is only commanded when the value (or the action) changes.
 *  - Block not enabled: what the bridge does is set by when_not_active (COAST / BRAKE / HOLD).
 *  - ENO is true while the bridge follows the block this pass.
 *
 *  The device checks the range (a full bridge takes -100..100, a half bridge 0..100); an out of range value is an
 *  error, never clamped. A magnitude below 0.5 % brakes (the device's rule).
 */

//#block-enum @alias H-Bridge Action
typedef enum {
  VM_HBRIDGE_ACTION_DRIVE = 0,  //@alias Drive @description Follow the Drive input (the default: 0 is what a fresh block holds)
  VM_HBRIDGE_ACTION_BRAKE = 1,  //@alias Brake @description Short the motor terminals
  VM_HBRIDGE_ACTION_COAST = 2,  //@alias Coast @description Release the motor (outputs high-Z)
} vm_hbridge_action_e;

//#block-enum @alias H-Bridge Idle Action
typedef enum {
  VM_HBRIDGE_IDLE_COAST = 0,  //@alias Coast @description Release the motor (the default: 0 is what a fresh block holds)
  VM_HBRIDGE_IDLE_BRAKE = 1,  //@alias Brake @description Short the motor terminals
  VM_HBRIDGE_IDLE_HOLD = 2,   //@alias Hold @description Leave the bridge as it is
} vm_hbridge_idle_e;

// What was last commanded; runtime only.
typedef enum {
  VM_HBRIDGE_LAST_NONE = 0,
  VM_HBRIDGE_LAST_DRIVE = 1,
  VM_HBRIDGE_LAST_BRAKE = 2,
  VM_HBRIDGE_LAST_COAST = 3,
} vm_hbridge_last_e;

//@data vm_block_hbridge_data_t
typedef struct __attribute__((aligned(4))) {
  float    drive;            // @description Drive in percent (-100..100) when the Drive input is unwired
  uint8_t  device_id;        // @description H-bridge device @id device @contract packet_sys_hbridge_set_drive_t
  uint8_t  channel;          // @description Channel of the device to drive @id pin @device-field device_id
  uint8_t  action;           // @description What the block does while it is enabled @enum-ref vm_hbridge_action_e
  uint8_t  when_not_active;  // @description What the bridge does while the block is not enabled @enum-ref vm_hbridge_idle_e
  uint8_t  last;             // @description vm_hbridge_last_e, what was commanded last @runtime
  uint8_t  _pad;             // @description Align the next field
  int16_t  last_x10;         // @description Drive last sent, in tenths of a percent @runtime
} vm_block_hbridge_data_t;

_Static_assert(sizeof(vm_block_hbridge_data_t) == 12, "vm_block_hbridge_data_t must be 12 bytes");
#define VM_HBRIDGE_CUSTOM_LEN sizeof(vm_block_hbridge_data_t)

/* Pins, by index: VM_IN_<BLOCK>_<PIN>. Titles and descriptions are in hbridge.display.json. */
//#block-enum @alias H-Bridge Inputs
typedef enum vm_in_hbridge_e {
  VM_IN_HBRIDGE_DRIVE = 0,  //@in @value f32 @overrides drive
} vm_in_hbridge_e;

/* Load-time check of the block's state; the pin shape comes from the pin enum above. */
//@rule action is a vm_hbridge_action_e value and when_not_active a vm_hbridge_idle_e value. @error ERR_VM_BLK_BAD_SHAPE
//@rule drive is a finite number. @error ERR_VM_BLK_BAD_SHAPE
bool vm_verify_hbridge(vm_block_h b);
/* The body, called every pass.
   Face, titles and descriptions: hbridge.display.json; the app's hbridge.content.json is generated from this header. */
//#vm-block VM_BLK_HBRIDGE @id 20
//@activation enabled
void vm_blk_hbridge(vm_block_h b);
