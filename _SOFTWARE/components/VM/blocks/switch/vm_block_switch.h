#pragma once
#include "vm_block_helpers.h"

/*
 *           -------------
 *  ->EN     |  SWITCH   | ->ENO
 *  ->SEL    |           | ->CASE0..n
 *           -------------
 *
 *  VM_BLK_SWITCH -- up to 16-way flow router. Enable-driven and stateless (custom_len = 0).
 */

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's //@in / //@out says what the pin carries and
   which state field it replaces while unwired; titles and descriptions are in switch.display.json. */
//#block-enum @alias Switch Inputs
typedef enum vm_in_switch_e {
  VM_IN_SWITCH_SELECTOR = 0,   //@in @value i32 @required
} vm_in_switch_e;

//#block-enum @alias Switch Outputs
typedef enum vm_out_switch_e {
  VM_OUT_SWITCH_BRANCH_ANY = 0,   //@out @value gate @required @repeat
} vm_out_switch_e;

/* The body, called every pass.
   Face, titles and descriptions: switch.display.json; the app's switch.content.json is generated from this header. */
//#vm-block VM_BLK_SWITCH @id 4
//@activation enabled
void vm_blk_switch(vm_block_h b);
