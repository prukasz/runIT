#pragma once
#include "vm_block_helpers.h"
#include "vm_block_timer.h"  // vm_timer_unit_e and its ms scale
#include "vm_exec.h"

/*
 *            -------------
 *  ->EN      |           | ->ENO
 *  ->PERIOD  | PERIODIC  | ->TICK
 *            -------------
 *
 *  VM_BLK_PERIODIC -- "Every N": a one-pass pulse on ENO (and TICK) every PERIOD
 *  while enabled. The root of a timed chain.
 *
 *  - The first tick is on the pass the block becomes enabled, then every PERIOD.
 *    Disabling stops it; enabling again restarts the phase.
 *  - The deadline advances by exactly PERIOD, so the rate doesn't drift. A pass
 *    that arrives after the deadline ticks at once (late, not skipped).
 *  - Never more than one tick per pass: ticks a slow pass missed entirely are
 *    dropped, not burst. Dropping is reported once per overrun episode
 *    (ERR_VM_PERIODIC_OVERRUN) and re-armed by the next on-time tick. The pass
 *    (10 ms floor) is the resolution: a shorter PERIOD overruns every pass.
 *  - PERIOD comes from the pin, or from custom_data when the pin is unwired, in
 *    `time_base` units. A PERIOD of 0 from the pin pauses ticking.
 *
 *  custom_data layout (16 bytes, 8-aligned):
 *    [0..3]   u32 period     Fallback when PERIOD is unwired (> 0), in time_base units
 *    [4]      u8  time_base  vm_timer_unit_e (0=ms, 1=s, 2=min, 3=h)
 *    [5]      u8  flags      VM_PERIODIC_F_* (runtime; 0 on the wire)
 *    [6..7]   u16 _pad
 *    [8..15]  u64 next_ms    Next deadline (runtime; 0 on the wire)
 */

#define VM_PERIODIC_F_ARMED (1u << 0)    // Running: next_ms is valid
#define VM_PERIODIC_F_OVERRUN (1u << 1)  // Current overrun episode already reported

typedef struct __attribute__((aligned(8))) {
  uint32_t period;    // In time_base units
  uint8_t time_base;  // Unit of period @enum-ref vm_timer_unit_e
  uint8_t flags;      // VM_PERIODIC_F_* @runtime
  uint16_t _pad;
  uint64_t next_ms;   // Next deadline @runtime
} vm_block_periodic_data_t;

_Static_assert(sizeof(vm_block_periodic_data_t) == 16, "vm_block_periodic_data_t must be 16 bytes");
_Static_assert(offsetof(vm_block_periodic_data_t, time_base) == 4, "time_base must be at offset 4");
_Static_assert(offsetof(vm_block_periodic_data_t, next_ms) == 8, "next_ms must be at offset 8");
#define VM_PERIODIC_CUSTOM_LEN sizeof(vm_block_periodic_data_t)

#define VM_PERIODIC_IN_PERIOD 0u
#define VM_PERIODIC_TICK 0u

static inline bool vm_verify_periodic(vm_block_h b) {
  vm_block_periodic_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));
  if (d.time_base >= VM_TIMER_UNIT_CNT) return false;
  // An unwired PERIOD pin needs a real constant.
  if (!vm_block_optional_in(b, VM_PERIODIC_IN_PERIOD) && d.period == 0) return false;
  return true;
}

/* Pure state transition: true when this pass ticks; *missed = ticks dropped. */
static inline bool vm_periodic_step(vm_block_periodic_data_t* d, uint64_t period_ms, uint64_t now, uint32_t* missed) {
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

/* Enable-driven. TICK and ENO pulse together: loud on a tick, quiet otherwise. */
static inline void vm_blk_periodic(vm_block_h b) {
  vm_block_periodic_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));

  if (!vm_block_is_enabled(b)) {
    state.flags &= (uint8_t)~(VM_PERIODIC_F_ARMED | VM_PERIODIC_F_OVERRUN);
    memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
    vm_block_drive_gate(b, VM_PERIODIC_TICK, false);
    vm_block_set_eno(b, false);
    return;
  }

  uint32_t period = state.period;
  if (!vm_block_check(b, VM_BLOCK_GET_PARAM(period, b, VM_PERIODIC_IN_PERIOD, state.period))) {
    vm_block_drive_gate(b, VM_PERIODIC_TICK, false);
    vm_block_set_eno(b, false);
    return;
  }
  const uint64_t period_ms = (uint64_t)period * vm_timer_unit_scale_ms(state.time_base);
  if (period_ms == 0) {  // paused from the pin
    state.flags &= (uint8_t)~VM_PERIODIC_F_ARMED;
    memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
    vm_block_drive_gate(b, VM_PERIODIC_TICK, false);
    vm_block_set_eno(b, false);
    return;
  }

  uint64_t now = vm_now_ms();
  if (unlikely(now == 0)) now = vm_clock_us() / 1000;
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

  vm_block_drive_gate(b, VM_PERIODIC_TICK, tick);
  vm_block_set_eno(b, tick);
}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_PERIODIC @title Every @category time @state vm_block_periodic_data_t @activation enabled Runs every pass while enabled.
//@block-description Pulses ENO and TICK for one pass every period while enabled; never bursts after a slow pass.
//@rule time_base is a vm_timer_unit_e value. @error ERR_VM_BLK_BAD_SHAPE
//@rule With the period input unwired, period is not 0. @error ERR_VM_BLK_BAD_SHAPE
//@in 0 period @title Period @description Overrides period, in time_base units; 0 pauses. @value u32
//@out 0 tick @title Tick @value gate
#define VM_BLOCK_TYPE_PERIODIC \
  {.run = vm_blk_periodic, .check = vm_verify_periodic, .min_in = 0, .min_q = 0, .required_in = 0x0u, .state_len = VM_PERIODIC_CUSTOM_LEN}
