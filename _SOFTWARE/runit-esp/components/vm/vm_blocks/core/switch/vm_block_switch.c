#include "vm_block_switch.h"
#include "vm_blocks.h"
#include "vm_branch_core.h"

void vm_blk_switch(vm_block_h b) {
  const vm_accessor_t* in0 = vm_block_get_inputs(b)[VM_IN_SWITCH_SELECTOR];
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

VM_BLOCK_TYPE_DEFINE(VM_BLK_SWITCH,
                     .run = vm_blk_switch, .min_in = 1, .max_in = 1, .min_q = 1, .max_q = CONFIG_VM_BLOCK_MAX_OUT, .required_in = 0x1u);
