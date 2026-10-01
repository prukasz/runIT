#include "vm_block_on_event.h"

static bool vm_on_event_field(uint8_t want, uint8_t got) {
  return want == VM_EVENT_ANY || want == got;
}

static bool vm_on_event_matches(const vm_block_on_event_data_t* f, const sys_event_t* ev) {
  return vm_on_event_field(f->domain, ev->domain) && vm_on_event_field(f->device_id, ev->device_id) &&
         vm_on_event_field(f->channel, ev->channel) && vm_on_event_field(f->event, ev->event);
}

/* Enable-driven: acts only on a pass that holds a matching event. */
void vm_blk_on_event(vm_block_h b) {
  if (!vm_block_is_enabled(b)) {
    vm_block_set_eno(b, false);
    return;
  }

  vm_block_on_event_data_t filter;
  memcpy(&filter, vm_block_get_custom_data(b), sizeof(filter));

  uint32_t matches = 0;
  int32_t value = 0;
  const uint8_t n = vm_event_count();
  for (uint8_t i = 0; i < n; i++) {
    const sys_event_t* ev = vm_event_at(i);
    if (ev && vm_on_event_matches(&filter, ev)) {
      matches++;
      value = ev->value;
    }
  }

  if (matches == 0) {
    vm_block_set_eno(b, false);
    return;
  }

  if (b->cfg.q_cnt > VM_OUT_ON_EVENT_VALUE) {
    BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(value, vm_block_get_outputs(b)[VM_OUT_ON_EVENT_VALUE], 0), b);
  }
  if (b->cfg.q_cnt > VM_OUT_ON_EVENT_COUNT) {
    BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(matches, vm_block_get_outputs(b)[VM_OUT_ON_EVENT_COUNT], 0), b);
  }
  vm_block_set_eno(b, !vm_block_failed(b));
}
