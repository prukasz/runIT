#include "vm_block_latch.h"

bool vm_verify_latch(vm_block_h b) {
  vm_block_latch_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));
  return d.mode < VM_LATCH_MODE_CNT;
}

/* Pure state transition. */
static bool vm_latch_step(uint8_t mode, bool held, bool set, bool reset) {
  return (mode == VM_LATCH_RESET_DOMINANT) ? (!reset && (set || held)) : (set || (held && !reset));
}

/* EN is Set, not an execution gate. Always read Reset so it works after EN
 * falls. A failed read holds the state and drops ENO for this pass. */
void vm_blk_latch(vm_block_h b) {
  vm_block_latch_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));
  const bool held = (state.flags & VM_LATCH_F_HELD) != 0;

  const bool set = vm_block_is_enabled(b);
  bool reset = false;
  if (!vm_block_check(b, VM_BLOCK_GET_PARAM(reset, b, VM_IN_LATCH_RESET, false)) || vm_block_failed(b)) {
    vm_block_set_eno(b, false);
    return;
  }

  const bool next = vm_latch_step(state.mode, held, set, reset);
  state.flags = (uint8_t)((state.flags & ~VM_LATCH_F_HELD) | (next ? VM_LATCH_F_HELD : 0u));
  memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
  vm_block_set_eno(b, next);
}
