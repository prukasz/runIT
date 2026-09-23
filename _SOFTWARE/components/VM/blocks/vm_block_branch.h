#pragma once
#include "vm_block_helpers.h"

#define VM_BRANCH_CUSTOM_LEN 0u
#define VM_BRANCH_NONE 0xFFu

/*
 *           -------------                -------------
 *  ->EN     |    IF     | ->ENO  ->EN    |   SWITCH  | ->ENO
 *  ->COND   |           | ->TRUE ->SEL   |           | ->CASE0..n
 *           |           | ->FALSE        |           |
 *           -------------                -------------
 *
 *  Flow routers -- VM_BLK_IF (two-way) and VM_BLK_SWITCH (up to 16-way).
 *  Enable-driven and stateless (custom_len = 0). Outputs act as enable gates for child blocks.
 */

static inline void vm_branch_drive(vm_block_h b, uint8_t taken) {
  for (uint8_t pin = 0; pin < b->cfg.q_cnt; ++pin) {
    vm_block_drive_gate(b, pin, pin == taken);
  }
}

static inline void vm_blk_if(vm_block_h b) {
  const vm_accessor_t* in0 = vm_block_get_inputs(b)[0];
  uint8_t taken = VM_BRANCH_NONE;

  IF_BLOCK_ENABLED(b) {
    bool cond = false;
    if (vm_block_check(b, VM_OBJ_SCALAR_GET(cond, in0))) taken = cond ? 0u : 1u;
  }

  vm_branch_drive(b, taken);
  vm_block_set_eno(b, (taken != VM_BRANCH_NONE) && !vm_block_failed(b));
}

static inline void vm_blk_switch(vm_block_h b) {
  const vm_accessor_t* in0 = vm_block_get_inputs(b)[0];
  uint8_t taken = VM_BRANCH_NONE;

  IF_BLOCK_ENABLED(b) {
    int32_t sel = 0;
    if (vm_block_check(b, VM_OBJ_SCALAR_GET(sel, in0)) && sel >= 0 && sel < (int32_t)b->cfg.q_cnt) {
      taken = (uint8_t)sel;
    }
  }

  vm_branch_drive(b, taken);
  vm_block_set_eno(b, (taken != VM_BRANCH_NONE) && !vm_block_failed(b));
}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_IF @title If @category flow
//@block-description Two-way router: output 0 when the condition is non-zero, else output 1. Outputs are flow (enable sources below), cleared when the block doesn't decide.
//@in 0 condition @title Condition
//@out 0 yes @title Yes
//@out 1 no @title No
#define VM_BLOCK_TYPE_IF \
  {.run = vm_blk_if, .check = NULL, .min_in = 1, .min_q = 2, .required_in = 0x1u, .state_len = VM_BRANCH_CUSTOM_LEN}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_SWITCH @title Switch @category flow
//@block-description N-way router: drives the output whose index equals the selector (rounded); out of range drives none.
//@in 0 selector @title Selector
//@out * branch @title Branch @description One per case, 0..15.
#define VM_BLOCK_TYPE_SWITCH \
  {.run = vm_blk_switch, .check = NULL, .min_in = 1, .min_q = 1, .required_in = 0x1u, .state_len = VM_BRANCH_CUSTOM_LEN}
