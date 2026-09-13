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

static inline bool vm_verify_set(vm_block_h b) {
  return vm_block_shape_valid(b, 2, 0, 0x3u);
}

static inline void vm_blk_set(vm_block_h b) {
  const vm_accessor_t* src = NULL;
  const vm_accessor_t* dst = NULL;

  if (likely(vm_block_require(b, 2, 0, 0x3u))) {
    src = vm_block_get_inputs(b)[VM_SET_IN_SRC];
    dst = vm_block_get_inputs(b)[VM_SET_IN_DST];

    // check if anything new to set
    if (vm_block_triggered_by(b, VM_SET_IN_SRC)) {
      // check if block enabled (usually no input connected)
      IF_BLOCK_ENABLED(b) {
        // copy to target and report error if occured
        BLOCK_CALL(vm_block_obj_copy_content(src, dst), b);
        if (likely(!g_vm_block_fault)) {
          vm_block_set_eno(b, true);
          return;
        }
      }
    }
  }
  // case when error or non activated
  vm_block_set_eno(b, false);
}
