#include "vm_block_if.h"
#include "vm_branch_core.h"

void vm_blk_if(vm_block_h b) {
  const vm_accessor_t* in0 = vm_block_get_inputs(b)[VM_IF_IN_CONDITION];
  uint8_t taken = VM_BRANCH_NONE;

  IF_BLOCK_ENABLED(b) {
    bool cond = false;
    if (vm_block_check(b, VM_OBJ_SCALAR_GET(cond, in0))) taken = cond ? 0u : 1u;
  }

  vm_branch_drive(b, taken);
  vm_block_set_eno(b, (taken != VM_BRANCH_NONE) && !vm_block_failed(b));
}
