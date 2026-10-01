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

#define VM_IF_IN_CONDITION 0u

/* The body, called every pass. */
void vm_blk_if(vm_block_h b);

//#vm-block VM_BLK_IF @id 3
//@title If
//@category flow
//@activation enabled Runs every pass while enabled.
//@block-description Two-way router: output 0 when the condition is non-zero, else output 1. Outputs are flow (enable sources below), cleared when the block doesn't decide.
//@in 0 condition @title Condition @value bool @required
//@out 0 yes @title Yes @value gate @required
//@out 1 no @title No @value gate @required
