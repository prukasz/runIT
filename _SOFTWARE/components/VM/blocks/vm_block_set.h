#pragma once
#include "vm_block_helpers.h"

/*
 *           -------------
 *  ->EN     |           | ->ENO
 *  ->SOURCE |    SET    |
 *  ->DST    |           |
 *           -------------
 *
 *  VM_BLK_SET -- Deep payload copy (IN0: src -> IN1: dst). Update-driven, stateless.
 *  Copies whole trees into an existing matching shape without allocation. Types and
 *  element counts must match exactly (no conversion). Both pins are inputs; ENO
 *  signals a copy completed this pass.
 */

#define VM_SET_CUSTOM_LEN 0u

#define VM_SET_IN_SRC 0u  // source -- the pin whose freshness fires the block
#define VM_SET_IN_DST 1u  // target -- named, not read

static inline void vm_blk_set(vm_block_h b) {
  const vm_accessor_t* src = vm_block_get_inputs(b)[VM_SET_IN_SRC];
  const vm_accessor_t* dst = vm_block_get_inputs(b)[VM_SET_IN_DST];

  // check if anything new to set
  if (vm_block_triggered_by(b, VM_SET_IN_SRC)) {
    // check if block enabled (usually no input connected)
    IF_BLOCK_ENABLED(b) {
      // copy to target and report error if occured
      BLOCK_CALL(vm_block_obj_copy_content(src, dst), b);
      if (likely(!vm_block_failed(b))) {
        vm_block_set_eno(b, true);
        return;
      }
    }
  }
  // case when error or non activated
  vm_block_set_eno(b, false);
}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_SET @title Set @category data @activation triggered Runs when the source (input 0) is fresh and the block is enabled.
//@block-description Copies the source payload (whole trees included) into the destination when the source is fresh. Types and counts must match.
//@in 0 source @title Source @value object
//@in 1 destination @title Destination @value object
#define VM_BLOCK_TYPE_SET \
  {.run = vm_blk_set, .check = NULL, .min_in = 2, .min_q = 0, .required_in = 0x3u, .state_len = VM_SET_CUSTOM_LEN}
