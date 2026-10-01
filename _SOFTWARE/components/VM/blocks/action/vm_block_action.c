#include "vm_block_action.h"

bool vm_verify_action(vm_block_h b) {
  vm_block_action_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));
  if (d.scope > VM_ACTION_SCOPE_RECORDED) return false;
  if (!vm_block_optional_in(b, VM_ACTION_IN_ID) && d.action_id == 0) return false;
  return true;
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
  if (!vm_block_check(b, VM_BLOCK_GET_PARAM(id, b, VM_ACTION_IN_ID, state.action_id))) {
    vm_block_set_eno(b, false);
    return;
  }
  if (unlikely(id == 0 || id > UINT8_MAX)) {
    BLOCK_CALL(VM_BLK_ERR_NEW(ERR_INVALID_VAL_UI32, .val = id, .min = 1, .max = UINT8_MAX), b);
    vm_block_set_eno(b, false);
    return;
  }

  BLOCK_CALL(vm_exec_request_action(state.scope, (uint8_t)id), b);
  vm_block_set_eno(b, !vm_block_failed(b));
}
