#pragma once
#include "vm_block_helpers.h"
#include "vm_exec.h"

/*
 *           -------------
 *  ->EN     |   TIMER   | ->ENO
 *  ->IN     | (TON/TOF/ | ->Q
 *  ->PT     |    TP)    | ->ET
 *           -------------
 *
 *  VM_BLK_TIMER -- IEC standard timer (TON, TOF, TP, plus inverted variants).
 *  Computes Q state and elapsed time ET from input trigger and preset duration PT.
 *  Time base is load-configurable: ms (0), s (1), min (2), hr (3).
 *
 *  custom_data layout:
 *    [0]      u8  mode          vm_timer_mode_e (TON, TOF, TP, ...)
 *    [1]      u8  flags         VM_TIMER_F_*
 *    [2]      u8  time_base     vm_timer_unit_e (0=ms, 1=s, 2=min, 3=h)
 *    [3]      u8  _pad1
 *    [4..7]   u32 _pad2
 *    [8..11]  u32 pt            Static preset time in configured unit
 *    [12..15] u32 _align
 *    [16..23] u64 start_ms      Reference timestamp
 *    [24..27] u32 elapsed       Elapsed time in configured unit
 *    [28..31] u32 _pad_tail
 */

//#ref-enum @alias Timer Mode
typedef enum {
  VM_TIMER_TON      = 0,  // On-Delay
  VM_TIMER_TOF      = 1,  // Off-Delay
  VM_TIMER_TP       = 2,  // Pulse Timer
  VM_TIMER_TON_INV  = 3,  // Inverted On-Delay (!Q)
  VM_TIMER_TOF_INV  = 4,  // Inverted Off-Delay (!Q)
  VM_TIMER_TP_INV   = 5,  // Inverted Pulse Timer (!Q)
  VM_TIMER_MODE_CNT = 6,
} vm_timer_mode_e;

//#ref-enum @alias Time Unit
typedef enum {
  VM_TIMER_UNIT_MS  = 0,  // Milliseconds (1 ms)
  VM_TIMER_UNIT_SEC = 1,  // Seconds (1,000 ms)
  VM_TIMER_UNIT_MIN = 2,  // Minutes (60,000 ms)
  VM_TIMER_UNIT_HR  = 3,  // Hours (3,600,000 ms)
  VM_TIMER_UNIT_CNT = 4,
} vm_timer_unit_e;

static inline uint64_t vm_timer_unit_scale_ms(uint8_t unit) {
  switch (unit) {
    case VM_TIMER_UNIT_SEC: return 1000ULL;
    case VM_TIMER_UNIT_MIN: return 60000ULL;
    case VM_TIMER_UNIT_HR:  return 3600000ULL;
    case VM_TIMER_UNIT_MS:
    default:                return 1ULL;
  }
}

static inline const char* vm_timer_unit_name(vm_timer_unit_e u) {
  switch (u) {
    case VM_TIMER_UNIT_MS:  return "ms";
    case VM_TIMER_UNIT_SEC: return "s";
    case VM_TIMER_UNIT_MIN: return "min";
    case VM_TIMER_UNIT_HR:  return "h";
    default:                return "?";
  }
}

#define VM_TIMER_F_INITIALIZED (1u << 0)
#define VM_TIMER_F_RUNNING     (1u << 1)
#define VM_TIMER_F_PREV_IN     (1u << 2)
#define VM_TIMER_F_INVERTED    (1u << 3)

typedef struct __attribute__((aligned(8))) {
  uint8_t  mode;        // vm_timer_mode_e @enum-ref vm_timer_mode_e
  uint8_t  flags;       // VM_TIMER_F_*: only VM_TIMER_F_INVERTED (0x08) is set by the app
  uint8_t  time_base;   // Unit of pt and ET @enum-ref vm_timer_unit_e
  uint8_t  _pad1;
  uint32_t _pad2;
  uint32_t pt;          // Preset time in configured unit (hardcoded fallback)
  uint64_t start_ms;    // Timestamp when timing started @runtime
  uint32_t elapsed;     // Current elapsed time @runtime
} vm_block_timer_data_t;

_Static_assert(sizeof(vm_block_timer_data_t) == 32, "vm_block_timer_data_t must be 32 bytes");
_Static_assert(offsetof(vm_block_timer_data_t, time_base) == 2, "time_base must be at offset 2");
_Static_assert(offsetof(vm_block_timer_data_t, pt) == 8, "pt must be at offset 8");
_Static_assert(offsetof(vm_block_timer_data_t, start_ms) == 16, "start_ms must be at offset 16");
_Static_assert(offsetof(vm_block_timer_data_t, elapsed) == 24, "elapsed must be at offset 24");

/**
 * @brief Initialize timer configuration in block custom data.
 */
static inline void vm_block_timer_init_data(void* buffer, vm_timer_mode_e mode, uint32_t pt, bool inverted) {
  const vm_block_timer_data_t data = {
      .mode = (uint8_t)mode,
      .time_base = (uint8_t)VM_TIMER_UNIT_MS,
      .pt = pt,
      .flags = inverted ? VM_TIMER_F_INVERTED : 0,
  };
  memcpy(buffer, &data, sizeof(data));
}

static inline void vm_block_timer_init_data_ex(void* buffer, vm_timer_mode_e mode, vm_timer_unit_e unit, uint32_t pt, bool inverted) {
  const vm_block_timer_data_t data = {
      .mode = (uint8_t)mode,
      .time_base = (uint8_t)unit,
      .pt = pt,
      .flags = inverted ? VM_TIMER_F_INVERTED : 0,
  };
  memcpy(buffer, &data, sizeof(data));
}

#define VM_TIMER_IN_SIGNAL 0u
#define VM_TIMER_IN_PT 1u
#define VM_TIMER_Q 0u
#define VM_TIMER_ET 1u

static inline bool vm_timer_elapsed(vm_block_timer_data_t* d, uint32_t pt, uint64_t now) {
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

static inline void vm_timer_start(vm_block_timer_data_t* d, uint64_t now) {
  d->flags |= VM_TIMER_F_RUNNING;
  d->start_ms = now;
  d->elapsed = 0;
}

/* Pure state transition: no accessors, outputs, clock reads, or diagnostics. */
static inline bool vm_timer_step(vm_block_timer_data_t* d, bool in_val, uint32_t pt, uint64_t now) {
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

static inline bool vm_verify_timer(vm_block_h b) {
  vm_block_timer_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));
  return d.mode < VM_TIMER_MODE_CNT && d.time_base < VM_TIMER_UNIT_CNT;
}

/* Enable-driven. Disabled timers reset; Q/ET are value writes, ENO follows Q. */
static inline void vm_blk_timer(vm_block_h b) {
  vm_block_timer_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));
  if (!vm_block_is_enabled(b)) {
    state.flags &= (uint8_t)~(VM_TIMER_F_RUNNING | VM_TIMER_F_INITIALIZED | VM_TIMER_F_PREV_IN);
    state.elapsed = 0;
    memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
    vm_block_drive_gate(b, VM_TIMER_Q, false);
    vm_block_drive_gate(b, VM_TIMER_ET, false);
    vm_block_set_eno(b, false);
    return;
  }
  bool signal = false;
  uint32_t pt = 0;
  if (!vm_block_check(b, VM_OBJ_SCALAR_GET(signal, vm_block_get_inputs(b)[VM_TIMER_IN_SIGNAL])) ||
      !vm_block_check(b, VM_BLOCK_GET_PARAM(pt, b, VM_TIMER_IN_PT, state.pt))) {
    // case when error or non activated
    vm_block_set_eno(b, false);
    return;
  }
  uint64_t now = vm_now_ms();
  if (unlikely(now == 0)) now = vm_clock_us() / 1000;
  const bool q = vm_timer_step(&state, signal, pt, now);
  memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
  vm_block_set_eno(b, q);
  if (b->cfg.q_cnt > VM_TIMER_Q) {
    uint8_t value = q ? 1 : 0;
    BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(value, vm_block_get_outputs(b)[VM_TIMER_Q], 0), b);
  }
  if (b->cfg.q_cnt > VM_TIMER_ET) {
    BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(state.elapsed, vm_block_get_outputs(b)[VM_TIMER_ET], 0), b);
  }
}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_TIMER @title Timer @category time @state vm_block_timer_data_t @activation enabled Runs every pass while enabled.
//@block-description IEC timer: on-delay, off-delay or pulse (plus inverted). Q follows the timer, ENO follows Q.
//@rule mode is a vm_timer_mode_e value and time_base a vm_timer_unit_e value. @error ERR_VM_BLK_BAD_SHAPE
//@in 0 in @title Input @value bool
//@in 1 pt @title Preset @description Overrides pt, in time_base units. @value u32
//@out 0 q @title Q @value bool
//@out 1 et @title Elapsed @value u32
#define VM_BLOCK_TYPE_TIMER \
  {.run = vm_blk_timer, .check = vm_verify_timer, .min_in = 1, .min_q = 0, .required_in = 0x1u, .state_len = sizeof(vm_block_timer_data_t)}
