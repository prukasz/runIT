
#include "vm_blocks.h"
#include "vm_block_set.h"

bool vm_verify_set(vm_block_h b) {
  vm_block_set_data_t data;
  memcpy(&data, vm_block_get_custom_data(b), sizeof(data));
  return isfinite(data.value);
}

// Source unwired: write the constant, when an enable fires (once on the first pass without enables).
static void set_constant(vm_block_h b, vm_block_set_data_t* d, const vm_accessor_t* dst) {
  bool fire = b->cfg.en_cnt ? vm_block_en_triggered(b) : !(d->flags & VM_SET_F_DONE);
  if (fire) {
    IF_BLOCK_ENABLED(b) {
      BLOCK_CALL(VM_OBJ_SET_SCALAR_USR(d->value, dst), b);
      if (likely(!vm_block_failed(b))) {
        d->flags |= VM_SET_F_DONE;
        vm_block_set_eno(b, true);
        return;
      }
    }
  }
  vm_block_set_eno(b, false);
}

void vm_blk_set(vm_block_h b) {
  vm_block_set_data_t* d = (vm_block_set_data_t*)vm_block_get_custom_data(b);
  const vm_accessor_t* src = vm_block_get_inputs(b)[VM_IN_SET_SOURCE];
  const vm_accessor_t* dst = vm_block_get_inputs(b)[VM_IN_SET_DESTINATION];

  if (!src) {
    set_constant(b, d, dst);
    return;
  }

  // check if anything new to set
  if (vm_block_triggered_by(b, VM_IN_SET_SOURCE)) {
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

VM_BLOCK_TYPE_DEFINE(VM_BLK_SET,
                     .run = vm_blk_set, .check = vm_verify_set, .min_in = 2, .max_in = 2, .min_q = 0, .max_q = 0, .required_in = 0x2u, .state_len = sizeof(vm_block_set_data_t));
