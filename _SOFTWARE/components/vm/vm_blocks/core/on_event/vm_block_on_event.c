#include "vm_block_on_event.h"
#include "vm_blocks.h"

/* Enable-driven: acts only on a pass that holds a matching event. */
void vm_blk_on_event(vm_block_h b) {
  if (!vm_block_is_enabled(b)) {
    vm_block_set_eno(b, false);
    return;
  }

  vm_block_on_event_data_t filter;
  memcpy(&filter, vm_block_get_custom_data(b), sizeof(filter));

  bool matched = false;
  int32_t value = 0;
  const uint8_t n = vm_event_count();
  for (uint8_t i = 0; i < n; i++) {
    const sys_event_t* ev = vm_event_at(i);
    if (ev && sys_event_matches(filter.source, filter.event, filter.channel, ev)) {
      matched = true;
      value = ev->value;
    }
  }

  if (!matched) {
    vm_block_set_eno(b, false);
    return;
  }

  if (b->cfg.q_cnt > VM_OUT_ON_EVENT_VALUE) {
    BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(value, vm_block_get_outputs(b)[VM_OUT_ON_EVENT_VALUE], 0), b);
  }
  vm_block_set_eno(b, !vm_block_failed(b));
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_ON_EVENT,
                     .run = vm_blk_on_event, .min_q = 0, .max_q = 1, .state_len = sizeof(vm_block_on_event_data_t));
