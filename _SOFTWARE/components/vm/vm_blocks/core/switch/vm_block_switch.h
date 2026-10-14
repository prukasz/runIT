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

typedef enum vm_in_switch_e {
  VM_IN_SWITCH_SELECTOR = 0,
} vm_in_switch_e;

typedef enum vm_out_switch_e {
  VM_OUT_SWITCH_BRANCH_ANY = 0,  // One per case, 0..15.
} vm_out_switch_e;

/* The body, called every pass. */
void vm_blk_switch(vm_block_h b);
