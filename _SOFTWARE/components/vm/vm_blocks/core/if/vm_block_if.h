#pragma once
#include "vm_block_helpers.h"

/*
 *           -------------
 *  ->EN     |    IF     | ->ENO
 *  ->COND   |           | ->TRUE
 *           |           | ->FALSE
 *           -------------
 *
 *  VM_BLK_IF -- two-way flow router. Enable-driven and stateless (custom_len = 0).
 */

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's tags: @alias (the name a user sees), @value, ... (vm-block-annotations.md).
   An unwired pin uses the state field it @overrides. */
typedef enum vm_in_if_e {
  VM_IN_IF_CONDITION = 0,  //@alias Condition @value bool @required
} vm_in_if_e;

typedef enum vm_out_if_e {
  VM_OUT_IF_YES = 0,  //@alias Yes @value gate @required
  VM_OUT_IF_NO,  //@alias No @value gate @required
} vm_out_if_e;

//#block VM_BLK_IF
//@alias If
//@category flow
//@description Two-way router: output 0 when the condition is non-zero, else output 1. Outputs are flow (enable sources below), cleared when the block doesn't decide.
//@activation enabled
//@when Runs every pass while enabled.
/* The body, called every pass. */
void vm_blk_if(vm_block_h b);
