#include "vm_block_edge.h"

bool vm_verify_edge(vm_block_h b) {
  vm_block_edge_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));
  return state.edge_type < VM_EDGE_TYPE_CNT;
}

/* EN is the observed signal, so a false EN must be sampled rather than
 * treated as a reason to skip or reset the block. A failed EN read emits no
 * pulse and leaves the previous sample intact. */
void vm_blk_edge(vm_block_h b) {
  const bool current = vm_block_is_enabled(b);
  if (vm_block_failed(b)) {
    vm_block_set_eno(b, false);
    return;
  }

  vm_block_edge_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));
  bool fired = false;
  if (state.flags & VM_EDGE_F_INITIALIZED) {
    const bool rising = !state.previous && current;
    const bool falling = state.previous && !current;
    fired = state.edge_type == VM_EDGE_RISING ? rising : state.edge_type == VM_EDGE_FALLING ? falling : rising || falling;
  }
  state.previous = current ? 1u : 0u;
  state.flags |= VM_EDGE_F_INITIALIZED;
  memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
  vm_block_set_eno(b, fired);
}
