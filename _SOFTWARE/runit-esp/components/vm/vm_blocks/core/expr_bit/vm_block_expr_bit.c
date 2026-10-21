#include "vm_block_expr_bit.h"
#include "vm_blocks.h"

bool vm_verify_expr_bit(vm_block_h b) {
  return vm_expr_check(b, vm_bit_ops, VM_BIT_OP_CNT);
}

void vm_blk_expr_bit(vm_block_h b) {
  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_get_custom_data(b);

  // Computed on every pass the block is enabled; ENO: the result was written.
  IF_BLOCK_ENABLED(b) {
    uint32_t r = 0;
    if (likely(vm_expr_eval_bit(b, c, &r))) {
      BLOCK_CALL(VM_OBJ_SET_SCALAR(r, vm_block_get_outputs(b)[0]), b);
      c->rt &= (uint8_t)~VM_EXPR_RT_FAULTED;
      vm_block_set_eno(b, !vm_block_failed(b));
      return;
    }
  }
  vm_block_set_eno(b, false);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_EXPR_BIT,
                     .run = vm_blk_expr_bit, .check = vm_verify_expr_bit, .min_in = 0, .max_in = CONFIG_VM_BLOCK_MAX_IN, .min_q = 1, .max_q = 1, .state_len = sizeof(vm_expr_code_t));
