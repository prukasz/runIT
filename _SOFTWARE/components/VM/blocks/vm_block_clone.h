#pragma once
#include "vm_block_helpers.h"

#define VM_CLONE_CUSTOM_LEN 0u

#define VM_CLONE_IN_SRC 0u  // source -- the pin whose freshness fires the block
#define VM_CLONE_IN_CELL 1u // target -- a pointer cell this block re-points

/*
 *           -------------
 *  ->EN     |           | ->ENO
 *  ->SOURCE |   CLONE   |
 *  ->CELL   |           |
 *           -------------
 *
 *  VM_BLK_CLONE -- Dynamic tree clone (IN0: src -> IN1: pointer cell). Update-driven, stateless.
 *  Like SET, but builds or reshapes the destination heap tree (vm_obj_dyn) dynamically to match
 *  source schema, then refills values without allocation on subsequent passes.
 */

static inline bool vm_verify_clone(vm_block_h b) {
  return vm_block_shape_valid(b, 2, 0, 0x3u);
}

static inline void vm_blk_clone(vm_block_h b) {
  const vm_accessor_t* src = NULL;
  const vm_accessor_t* cell = NULL;

  if (likely(vm_block_require(b, 2, 0, 0x3u))) {
    src = vm_block_get_inputs(b)[VM_CLONE_IN_SRC];
    cell = vm_block_get_inputs(b)[VM_CLONE_IN_CELL];
    /* IN0 alone, exactly as in a Set: the cell this block writes is an input
       pin too, and re-pointing it sets `upd` on its owner, so a trigger over
       both pins would fire the block on its own last allocation. */
    if (vm_block_triggered_by(b, VM_CLONE_IN_SRC)) {

      IF_BLOCK_ENABLED(b) {
        BLOCK_CALL(vm_block_obj_clone_into(src, cell), b);
        if (likely(!g_vm_block_fault)) {
          vm_block_set_eno(b, true);
          return;
        }
      }
    }
  }

  vm_block_set_eno(b, false);
}
