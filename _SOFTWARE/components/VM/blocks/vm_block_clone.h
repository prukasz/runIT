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

static inline void vm_blk_clone(vm_block_h b) {
  const vm_accessor_t* src = vm_block_get_inputs(b)[VM_CLONE_IN_SRC];
  const vm_accessor_t* cell = vm_block_get_inputs(b)[VM_CLONE_IN_CELL];

  /* IN0 alone, exactly as in a Set: the cell this block writes is an input
     pin too, and re-pointing it sets `upd` on its owner, so a trigger over
     both pins would fire the block on its own last allocation. */
  if (vm_block_triggered_by(b, VM_CLONE_IN_SRC)) {
    IF_BLOCK_ENABLED(b) {
      BLOCK_CALL(vm_block_obj_clone_into(src, cell), b);
      if (likely(!vm_block_failed(b))) {
        vm_block_set_eno(b, true);
        return;
      }
    }
  }

  vm_block_set_eno(b, false);
}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_CLONE @title Clone @category data
//@block-description Copies the source into a tree this block owns, hung off a pointer cell; rebuilds only when the source's shape changes.
//@in 0 source @title Source
//@in 1 cell @title Pointer cell
#define VM_BLOCK_TYPE_CLONE \
  {.run = vm_blk_clone, .check = NULL, .min_in = 2, .min_q = 0, .required_in = 0x3u, .state_len = VM_CLONE_CUSTOM_LEN}
