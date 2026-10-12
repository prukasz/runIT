#include "vm_block_action.h"
#include "vm_blocks.h"

bool vm_verify_action(vm_block_h b) {
  vm_block_action_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));
  return vm_block_optional_in(b, VM_IN_ACTION_ID) != NULL || d.action_id != 0;
}

/* Enable-driven, on the rising edge of the enable level. */
void vm_blk_action(vm_block_h b) {
  vm_block_action_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));

  const bool en = vm_block_is_enabled(b);
  const bool rising = en && !(state.flags & VM_ACTION_F_PREV_EN);
  state.flags = (uint8_t)(en ? (state.flags | VM_ACTION_F_PREV_EN) : (state.flags & ~VM_ACTION_F_PREV_EN));
  memcpy(vm_block_get_custom_data(b), &state, sizeof(state));

  if (!rising) {
    vm_block_set_eno(b, false);
    return;
  }

  uint32_t id = state.action_id;
  if (!vm_block_check(b, VM_BLOCK_GET_PARAM(id, b, VM_IN_ACTION_ID, state.action_id))) {
    vm_block_set_eno(b, false);
    return;
  }
  if (unlikely(id == 0)) {
    BLOCK_CALL(VM_BLK_ERR_NEW(ERR_VM_RANGE, .val = id, .min = 1, .max = UINT32_MAX), b);
    vm_block_set_eno(b, false);
    return;
  }

  BLOCK_CALL(vm_exec_request_action(id), b);
  vm_block_set_eno(b, !vm_block_failed(b));
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_ACTION,
                     .run = vm_blk_action, .check = vm_verify_action, .min_in = 0, .max_in = 1, .min_q = 0, .max_q = 0,
                     .state_len = sizeof(vm_block_action_data_t));
