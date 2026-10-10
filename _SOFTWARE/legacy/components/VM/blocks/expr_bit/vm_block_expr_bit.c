#include "vm_block_expr_bit.h"

bool vm_verify_expr_bit(vm_block_h b) {
  return vm_expr_check(b, vm_bit_ops, VM_BIT_OP_CNT);
}

void vm_blk_expr_bit(vm_block_h b) {
  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_get_custom_data(b);

  IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b) {
    uint32_t r = 0;
    if (unlikely(!vm_expr_eval_bit(b, c, &r))) return;
    vm_obj_h q = vm_block_get_outputs(b)[0];
    BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(r, q, 0), b);

    c->rt &= (uint8_t)~VM_EXPR_RT_FAULTED;
    if (likely(!vm_block_failed(b))) vm_block_set_eno(b, true);
    return;
  }

  // case when error or non activated
  vm_block_set_eno(b, false);
}
