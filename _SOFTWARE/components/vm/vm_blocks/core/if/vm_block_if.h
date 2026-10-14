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

typedef enum vm_in_if_e {
  VM_IN_IF_CONDITION = 0,
} vm_in_if_e;

typedef enum vm_out_if_e {
  VM_OUT_IF_YES = 0,
  VM_OUT_IF_NO,
} vm_out_if_e;

/* The body, called every pass. */
void vm_blk_if(vm_block_h b);
