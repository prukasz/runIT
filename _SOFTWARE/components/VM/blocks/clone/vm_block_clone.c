#include "vm_block_clone.h"

void vm_blk_clone(vm_block_h b) {
  const vm_accessor_t* src = vm_block_get_inputs(b)[VM_IN_CLONE_SOURCE];
  const vm_accessor_t* cell = vm_block_get_inputs(b)[VM_IN_CLONE_CELL];

  /* IN0 alone, exactly as in a Set: the cell this block writes is an input
     pin too, and re-pointing it sets `upd` on its owner, so a trigger over
     both pins would fire the block on its own last allocation. */
  if (vm_block_triggered_by(b, VM_IN_CLONE_SOURCE)) {
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
