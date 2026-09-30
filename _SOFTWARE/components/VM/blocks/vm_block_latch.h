#pragma once
#include "vm_block_helpers.h"

/*
 *           -------------
 *  ->EN     |           | ->ENO
 *  ->RESET  |   LATCH   |
 *           -------------
 *
 *  VM_BLK_LATCH -- Bistable (IEC 61131-3 SR / RS). Turns a one-pass pulse (EDGE,
 *  PERIODIC, an event) into a held level, so anything time-based below it can
 *  accumulate: event -> latch -> wait -> act.
 *
 *    set dominant   (SR): ENO = EN or (ENO and not RESET)
 *    reset dominant (RS): ENO = not RESET and (EN or ENO)
 *
 *  EN sets the held state; RESET clears it even when EN is false. RESET may be
 *  unwired (reads false), but EN must have at least one source. ENO reflects the
 *  held state, so it remains true after a one-pass EN pulse until RESET wins.
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

#define VM_LATCH_F_HELD (1u << 0)  // Current ENO state

typedef struct __attribute__((aligned(4))) {
  uint8_t mode;   // Which signal wins when EN and RESET are true @enum-ref vm_latch_mode_e
  uint8_t flags;  // VM_LATCH_F_* @runtime
  uint16_t _pad;
} vm_block_latch_data_t;

_Static_assert(sizeof(vm_block_latch_data_t) == 4, "vm_block_latch_data_t must be 4 bytes");
#define VM_LATCH_CUSTOM_LEN sizeof(vm_block_latch_data_t)

#define VM_LATCH_IN_RESET 0u

static inline bool vm_verify_latch(vm_block_h b) {
  if (b->cfg.en_cnt == 0 || b->cfg.in_cnt > 1 || b->cfg.q_cnt != 0) return false;
  vm_block_latch_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));
  return d.mode < VM_LATCH_MODE_CNT;
}

/* Pure state transition. */
static inline bool vm_latch_step(uint8_t mode, bool held, bool set, bool reset) {
  return (mode == VM_LATCH_RESET_DOMINANT) ? (!reset && (set || held)) : (set || (held && !reset));
}

/* EN is Set, not an execution gate. Always read Reset so it works after EN
 * falls. A failed read holds the state and drops ENO for this pass. */
static inline void vm_blk_latch(vm_block_h b) {
  vm_block_latch_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));
  const bool held = (state.flags & VM_LATCH_F_HELD) != 0;

  const bool set = vm_block_is_enabled(b);
  bool reset = false;
  if (!vm_block_check(b, VM_BLOCK_GET_PARAM(reset, b, VM_LATCH_IN_RESET, false)) || vm_block_failed(b)) {
    vm_block_set_eno(b, false);
    return;
  }

  const bool next = vm_latch_step(state.mode, held, set, reset);
  state.flags = (uint8_t)((state.flags & ~VM_LATCH_F_HELD) | (next ? VM_LATCH_F_HELD : 0u));
  memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
  vm_block_set_eno(b, next);
}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_LATCH
//@title Latch
//@category logic
//@activation enabled Samples EN and Reset every pass; EN sets the held state.
//@data vm_block_latch_data_t
//@block-description EN sets a held flow level; Reset clears it. ENO stays active until reset.
//@header Latch {mode}
//@rule At least one EN source is connected; no data outputs. @error ERR_VM_BLK_BAD_SHAPE
//@rule mode is a vm_latch_mode_e value. @error ERR_VM_BLK_BAD_SHAPE
//@in 0 reset @title Reset @value bool @macro VM_LATCH_IN_RESET
//@eno @title Held @description Held flow level: true after EN sets it, until Reset clears it.
#define VM_BLOCK_TYPE_LATCH \
  {.run = vm_blk_latch, .check = vm_verify_latch, .min_in = 0, .min_q = 0, .required_in = 0x0u, .state_len = VM_LATCH_CUSTOM_LEN}
