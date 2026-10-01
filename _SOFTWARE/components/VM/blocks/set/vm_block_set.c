#include "vm_block_set.h"

void vm_blk_set(vm_block_h b) {
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
