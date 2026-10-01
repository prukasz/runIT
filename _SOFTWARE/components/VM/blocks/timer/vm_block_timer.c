#include "vm_block_timer.h"

static bool vm_timer_elapsed(vm_block_timer_data_t* d, uint32_t pt, uint64_t now) {
  const uint64_t scale = vm_timer_unit_scale_ms(d->time_base);
  const uint64_t pt_ms_total = (uint64_t)pt * scale;
  const uint64_t elapsed = now >= d->start_ms ? now - d->start_ms : 0;
  if (elapsed >= pt_ms_total) {
    d->elapsed = pt;
    return true;
  }
  d->elapsed = (uint32_t)(elapsed / scale);
  return false;
}

static void vm_timer_start(vm_block_timer_data_t* d, uint64_t now) {
  d->flags |= VM_TIMER_F_RUNNING;
  d->start_ms = now;
  d->elapsed = 0;
}

/* Pure state transition: no accessors, outputs, clock reads, or diagnostics. */
static bool vm_timer_step(vm_block_timer_data_t* d, bool in_val, uint32_t pt, uint64_t now) {
  const bool first_scan = !(d->flags & VM_TIMER_F_INITIALIZED);
  const bool prev_in = (d->flags & VM_TIMER_F_PREV_IN) != 0;
  bool q = false;

  // Preserve wire aliases: an inverted mode and the inversion flag combine by XOR.
  const uint8_t base_mode = d->mode % 3u;
  const bool invert = (d->mode >= VM_TIMER_TON_INV) != ((d->flags & VM_TIMER_F_INVERTED) != 0);

  switch (base_mode) {
    case VM_TIMER_TON: {
      if (in_val) {
        if (first_scan || !(d->flags & VM_TIMER_F_RUNNING)) {
          vm_timer_start(d, now);
        } else {
          (void)vm_timer_elapsed(d, pt, now);
        }
        q = (d->elapsed >= pt);
      } else {
        d->flags &= ~VM_TIMER_F_RUNNING;
        d->elapsed = 0;
        q = false;
      }
      break;
    }

    case VM_TIMER_TOF: {
      if (in_val) {
        d->flags &= ~VM_TIMER_F_RUNNING;
        d->elapsed = 0;
        q = true;
      } else {
        if (!first_scan && prev_in) {
          // 1 -> 0 transition: start off-delay timing
          vm_timer_start(d, now);
          q = (pt > 0);
        } else if (d->flags & VM_TIMER_F_RUNNING) {
          if (vm_timer_elapsed(d, pt, now)) {
            d->flags &= ~VM_TIMER_F_RUNNING;
            q = false;
          } else {
            q = true;
          }
        } else {
          d->elapsed = pt;
          q = false;
        }
      }
      break;
    }

    case VM_TIMER_TP: {
      if (!first_scan && !prev_in && in_val && !(d->flags & VM_TIMER_F_RUNNING)) {
        // 0 -> 1 rising edge trigger
        vm_timer_start(d, now);
        q = (pt > 0);
      } else if (d->flags & VM_TIMER_F_RUNNING) {
        if (vm_timer_elapsed(d, pt, now)) {
          d->flags &= ~VM_TIMER_F_RUNNING;
          q = false;
        } else {
          q = true;
        }
      } else {
        d->elapsed = 0;
        q = false;
      }
      break;
    }

    default:
      break;
  }

  // Record history
  d->flags |= VM_TIMER_F_INITIALIZED;
  if (in_val) {
    d->flags |= VM_TIMER_F_PREV_IN;
  } else {
    d->flags &= ~VM_TIMER_F_PREV_IN;
  }

  // Apply optional inversion
  if (invert) {
    q = !q;
  }

  return q;
}

bool vm_verify_timer(vm_block_h b) {
  vm_block_timer_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));
  return d.mode < VM_TIMER_MODE_CNT && d.time_base < VM_TIMER_UNIT_CNT;
}

/* Enable-driven. Disabled timers reset and clear ET and ENO. Q is ENO, held as a level; ET is a value write. */
void vm_blk_timer(vm_block_h b) {
  vm_block_timer_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));
  if (!vm_block_is_enabled(b)) {
    state.flags &= (uint8_t)~(VM_TIMER_F_RUNNING | VM_TIMER_F_INITIALIZED | VM_TIMER_F_PREV_IN);
    state.elapsed = 0;
    memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
    vm_block_drive_gate(b, VM_OUT_TIMER_ET, false);
    vm_block_set_eno(b, false);
    return;
  }
  bool signal = false;
  uint32_t pt = 0;
  if (!vm_block_check(b, VM_OBJ_SCALAR_GET(signal, vm_block_get_inputs(b)[VM_IN_TIMER_IN])) ||
      !vm_block_check(b, VM_BLOCK_GET_PARAM(pt, b, VM_IN_TIMER_PT, state.pt))) {
    // case when error or non activated
    vm_block_set_eno(b, false);
    return;
  }
  const uint64_t now = vm_now_ms();  // program time, latched at the top of the pass; never 0 inside a pass
  const bool q = vm_timer_step(&state, signal, pt, now);
  memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
  vm_block_set_eno(b, q);
  if (b->cfg.q_cnt > VM_OUT_TIMER_ET) {
    BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(state.elapsed, vm_block_get_outputs(b)[VM_OUT_TIMER_ET], 0), b);
  }
}
