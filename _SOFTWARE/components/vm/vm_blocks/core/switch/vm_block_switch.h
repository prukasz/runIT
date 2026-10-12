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

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's tags: @alias (the name a user sees), @value, ... (vm-block-annotations.md).
   An unwired pin uses the state field it @overrides. */
typedef enum vm_in_switch_e {
  VM_IN_SWITCH_SELECTOR = 0,  //@alias Selector @value i32 @required
} vm_in_switch_e;

typedef enum vm_out_switch_e {
  VM_OUT_SWITCH_BRANCH_ANY = 0,  //@alias Branch @description One per case, 0..15. @value gate @required @repeat
} vm_out_switch_e;

//#block VM_BLK_SWITCH
//@alias Switch
//@category flow
//@description N-way router: drives the output whose index equals the selector (rounded); out of range drives none.
//@activation enabled
//@when Runs every pass while enabled.
/* The body, called every pass. */
void vm_blk_switch(vm_block_h b);
