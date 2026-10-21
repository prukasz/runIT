
#include "vm_blocks.h"
#include "vm_block_set.h"

bool vm_verify_set(vm_block_h b) {
  vm_block_set_data_t data;
  memcpy(&data, vm_block_get_custom_data(b), sizeof(data));
  return isfinite(data.value);
}

// EN -> destination = source (or the Value constant while the source is unwired), on every pass the block is enabled.
void vm_blk_set(vm_block_h b) {
  const vm_block_set_data_t* d = (const vm_block_set_data_t*)vm_block_get_custom_data(b);
  const vm_accessor_t* src = vm_block_get_inputs(b)[VM_IN_SET_SOURCE];
  const vm_accessor_t* dst = vm_block_get_inputs(b)[VM_IN_SET_DESTINATION];

  IF_BLOCK_ENABLED(b) {
    if (src) {
      BLOCK_CALL(vm_block_obj_copy_content(src, dst), b);
    } else {
      BLOCK_CALL(VM_OBJ_SET_SCALAR_USR(d->value, dst), b);
    }
    vm_block_set_eno(b, !vm_block_failed(b));
    return;
  }
  vm_block_set_eno(b, false);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_SET,
                     .run = vm_blk_set, .check = vm_verify_set, .min_in = 2, .max_in = 2, .min_q = 0, .max_q = 0, .required_in = 0x2u, .state_len = sizeof(vm_block_set_data_t));
