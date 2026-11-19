#include "vm_block_clone.h"
#include "vm_blocks.h"

void vm_blk_clone(vm_block_h b) {
  const vm_accessor_t* src = vm_block_get_inputs(b)[VM_IN_CLONE_SOURCE];
  const vm_accessor_t* cell = vm_block_get_inputs(b)[VM_IN_CLONE_CELL];

  /* On every pass the block is enabled. Cheap when nothing changed shape: the copy already in the cell is refilled in
     place; a new tree is built only when the source's shape (or a name in it) changes. */
  IF_BLOCK_ENABLED(b) {
    BLOCK_CALL(vm_block_obj_clone_into(src, cell), b);
    vm_block_set_eno(b, !vm_block_failed(b));
    return;
  }
  vm_block_set_eno(b, false);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_CLONE,
                     .run = vm_blk_clone, .min_in = 2, .max_in = 2, .min_q = 0, .max_q = 0, .required_in = 0x3u);
