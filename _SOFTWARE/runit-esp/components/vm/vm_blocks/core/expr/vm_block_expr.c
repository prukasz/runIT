#include "vm_block_expr.h"
#include "vm_blocks.h"

bool vm_verify_expr(vm_block_h b) {
  return vm_expr_check(b, vm_expr_ops, VM_EXPR_OP_CNT);
}

void vm_blk_expr(vm_block_h b) {
  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_get_custom_data(b);

  // ENO passes through: true on every pass the block is enabled and nothing failed, whether or not it computed.
  IF_BLOCK_ENABLED(b) {
    IF_BLOCK_TRIGGERED(b) {
      float r = 0.0f;
      if (unlikely(!vm_expr_eval_f(b, c, &r))) {
        vm_block_set_eno(b, false);
        return;
      }
      vm_obj_h q = vm_block_get_outputs(b)[0];
      BLOCK_CALL(VM_OBJ_SET_SCALAR(r, q), b);

      c->rt &= (uint8_t)~VM_EXPR_RT_FAULTED;
      if (unlikely(vm_block_failed(b))) {
        vm_block_set_eno(b, false);
        return;
      }
    }
    vm_block_set_eno(b, true);
    return;
  }

  // case when not enabled
  vm_block_set_eno(b, false);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_EXPR,
                     .run = vm_blk_expr, .check = vm_verify_expr, .min_in = 0, .max_in = CONFIG_VM_BLOCK_MAX_IN, .min_q = 1, .max_q = 1, .state_len = sizeof(vm_expr_code_t));
