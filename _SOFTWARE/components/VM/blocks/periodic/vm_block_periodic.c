#include "vm_block_periodic.h"

bool vm_verify_periodic(vm_block_h b) {
  vm_block_periodic_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));
  if (d.time_base >= VM_TIMER_UNIT_CNT) return false;
  // An unwired PERIOD pin needs a real constant.
  if (!vm_block_optional_in(b, VM_PERIODIC_IN_PERIOD) && d.period == 0) return false;
  return true;
}

/* Pure state transition: true when this pass ticks; *missed = ticks dropped. */
static bool vm_periodic_step(vm_block_periodic_data_t* d, uint64_t period_ms, uint64_t now, uint32_t* missed) {
  *missed = 0;
  if (!(d->flags & VM_PERIODIC_F_ARMED)) {
    d->flags |= VM_PERIODIC_F_ARMED;
    d->next_ms = now + period_ms;
    return true;
  }
  if (now < d->next_ms) return false;

  d->next_ms += period_ms;
  if (d->next_ms <= now) {
    const uint64_t behind = (now - d->next_ms) / period_ms + 1u;
    d->next_ms += behind * period_ms;
    *missed = behind > UINT32_MAX ? UINT32_MAX : (uint32_t)behind;
  }
  return true;
}

/* Enable-driven. ENO is the tick: loud for one pass on a tick, quiet otherwise. */
void vm_blk_periodic(vm_block_h b) {
  vm_block_periodic_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));

  if (!vm_block_is_enabled(b)) {
    state.flags &= (uint8_t)~(VM_PERIODIC_F_ARMED | VM_PERIODIC_F_OVERRUN);
    memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
    vm_block_set_eno(b, false);
    return;
  }

  uint32_t period = state.period;
  if (!vm_block_check(b, VM_BLOCK_GET_PARAM(period, b, VM_PERIODIC_IN_PERIOD, state.period))) {
    vm_block_set_eno(b, false);
    return;
  }
  const uint64_t period_ms = (uint64_t)period * vm_timer_unit_scale_ms(state.time_base);
  if (period_ms == 0) {  // paused from the pin
    state.flags &= (uint8_t)~VM_PERIODIC_F_ARMED;
    memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
    vm_block_set_eno(b, false);
    return;
  }

  const uint64_t now = vm_now_ms();  // program time, latched at the top of the pass; never 0 inside a pass
  uint32_t missed = 0;
  const bool tick = vm_periodic_step(&state, period_ms, now, &missed);

  if (missed) {
    /* Reported without marking the block failed: the tick itself is valid and
       on_error must not withdraw it. */
    if (!(state.flags & VM_PERIODIC_F_OVERRUN)) {
      state.flags |= VM_PERIODIC_F_OVERRUN;
      VM_BLK_EMIT_ERR(ERR_VM_PERIODIC_OVERRUN, .block_idx = b->cfg.block_idx, .missed = missed,
                      .period_ms = period_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)period_ms);
    }
  } else if (tick) {
    state.flags &= (uint8_t)~VM_PERIODIC_F_OVERRUN;
  }
  memcpy(vm_block_get_custom_data(b), &state, sizeof(state));

  vm_block_set_eno(b, tick);
}
