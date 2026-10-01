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

#define VM_SWITCH_IN_SELECTOR 0u

/* The body, called every pass. */
void vm_blk_switch(vm_block_h b);

//#vm-block VM_BLK_SWITCH @id 4
//@title Switch
//@category flow
//@activation enabled Runs every pass while enabled.
//@block-description N-way router: drives the output whose index equals the selector (rounded); out of range drives none.
//@in 0 selector @title Selector @value i32 @required
//@out * branch @title Branch @description One per case, 0..15. @value gate @required
