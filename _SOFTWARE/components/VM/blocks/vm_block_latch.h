#pragma once
#include "vm_block_helpers.h"

/*
 *           -------------
 *  ->EN     |           | ->ENO
 *  ->SET    |   LATCH   | ->Q
 *  ->RESET  |  (SR/RS)  |
 *           -------------
 *
 *  VM_BLK_LATCH -- Bistable (IEC 61131-3 SR / RS). Turns a one-pass pulse (EDGE,
 *  PERIODIC, an event) into a held level, so anything time-based below it can
 *  accumulate: event -> latch -> wait -> act.
 *
 *    set dominant   (SR): Q = SET or (Q and not RESET)
 *    reset dominant (RS): Q = not RESET and (SET or Q)
 *
 *  SET and RESET are levels (non-zero = true); one pass is enough to switch.
 *  Either may be unwired (reads false), not both. Q lives in the block's own
 *  state, so it outlasts the pulse that set it. ENO follows Q as a level (like
 *  TIMER). The Q output is written only when it changes, loudly, so update-driven
 *  blocks below see the transition. Disabled: Q is held, ENO drops quietly.
 *
 *  custom_data layout (4 bytes):
 *    [0]    u8  mode    vm_latch_mode_e
 *    [1]    u8  flags   VM_LATCH_F_* (runtime; 0 on the wire)
 *    [2..3] u16 _pad
 */

//#ref-enum @alias Latch Mode
typedef enum {
  VM_LATCH_SET_DOMINANT = 0,    // SR: SET wins when both are true
  VM_LATCH_RESET_DOMINANT = 1,  // RS: RESET wins when both are true
  VM_LATCH_MODE_CNT = 2,
} vm_latch_mode_e;

#define VM_LATCH_F_Q (1u << 0)        // Current state
#define VM_LATCH_F_WRITTEN (1u << 1)  // Q output published at least once

typedef struct __attribute__((aligned(4))) {
  uint8_t mode;   // Which input wins when both are true @enum-ref vm_latch_mode_e
  uint8_t flags;  // VM_LATCH_F_* @runtime
  uint16_t _pad;
} vm_block_latch_data_t;

_Static_assert(sizeof(vm_block_latch_data_t) == 4, "vm_block_latch_data_t must be 4 bytes");
#define VM_LATCH_CUSTOM_LEN sizeof(vm_block_latch_data_t)

#define VM_LATCH_IN_SET 0u
#define VM_LATCH_IN_RESET 1u
#define VM_LATCH_Q 0u

static inline bool vm_verify_latch(vm_block_h b) {
  // With neither input wired the latch could never change.
  if (!vm_block_optional_in(b, VM_LATCH_IN_SET) && !vm_block_optional_in(b, VM_LATCH_IN_RESET)) return false;
  vm_block_latch_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));
  return d.mode < VM_LATCH_MODE_CNT;
}

/* Pure state transition. */
static inline bool vm_latch_step(uint8_t mode, bool q, bool set, bool reset) {
  return (mode == VM_LATCH_RESET_DOMINANT) ? (!reset && (set || q)) : (set || (q && !reset));
}

/* Enable-driven. A failed input read leaves Q unchanged and drops ENO. */
static inline void vm_blk_latch(vm_block_h b) {
  vm_block_latch_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));
  const bool q = (state.flags & VM_LATCH_F_Q) != 0;

  if (!vm_block_is_enabled(b)) {
    vm_block_set_eno(b, false);
    return;
  }

  bool set = false;
  bool reset = false;
  if (!vm_block_check(b, VM_BLOCK_GET_PARAM(set, b, VM_LATCH_IN_SET, false)) ||
      !vm_block_check(b, VM_BLOCK_GET_PARAM(reset, b, VM_LATCH_IN_RESET, false))) {
    vm_block_set_eno(b, false);
    return;
  }

  const bool next = vm_latch_step(state.mode, q, set, reset);
  const bool publish = (next != q) || !(state.flags & VM_LATCH_F_WRITTEN);
  state.flags = (uint8_t)((state.flags & ~VM_LATCH_F_Q) | (next ? VM_LATCH_F_Q : 0u) | VM_LATCH_F_WRITTEN);
  memcpy(vm_block_get_custom_data(b), &state, sizeof(state));

  if (publish && b->cfg.q_cnt > VM_LATCH_Q) {
    uint8_t value = next ? 1 : 0;
    BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(value, vm_block_get_outputs(b)[VM_LATCH_Q], 0), b);
  }
  vm_block_set_eno(b, next);
}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_LATCH @title Latch @category logic @state vm_block_latch_data_t
//@block-description Set / reset bistable: turns a one-pass pulse into a held level. ENO follows Q.
//@in 0 set @title Set
//@in 1 reset @title Reset
//@out 0 q @title Q
#define VM_BLOCK_TYPE_LATCH \
  {.run = vm_blk_latch, .check = vm_verify_latch, .min_in = 0, .min_q = 0, .required_in = 0x0u, .state_len = VM_LATCH_CUSTOM_LEN}
