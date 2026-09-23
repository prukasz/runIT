#pragma once
#include "vm_block_helpers.h"
#include "vm_exec.h"

/*
 *           -------------
 *  ->EN     |           | ->ENO
 *  ->START  |    FOR    | ->IDX
 *  ->END    | (SPAN RUN)|
 *  ->STEP   |           |
 *           -------------
 *
 *  VM_BLK_FOR -- C-style float iterator loop with span claiming.
 *  Executes sub-blocks in span range for each turn within max_turns budget.
 *
 *  custom_data layout:
 *    [0..3]   vm_span_t span       The range this block owns (MUST be first)
 *    [4..7]   f32       k_start    Fallback when input 0 is unwired
 *    [8..11]  f32       k_end      Fallback when input 1 is unwired
 *    [12..15] f32       k_step     Fallback when input 2 is unwired
 *    [16..17] u16       max_turns  Hard turn budget to bound execution
 *    [18]     u8        op         VM_FOR_OP_*  (advance iterator)
 *    [19]     u8        cmp        VM_FOR_CMP_* (loop condition)
 *    [20]     u8        rt         Runtime latch (VM_FOR_RT_*; 0 on wire)
 *    [21..23] u8[3]     pad
 */

//#ref-enum @alias Loop Step Operation
typedef enum vm_for_op_e {
  VM_FOR_OP_ADD = 0,
  VM_FOR_OP_SUB,
  VM_FOR_OP_MUL,
  VM_FOR_OP_DIV,
  VM_FOR_OP_CNT
} vm_for_op_e;

//#ref-enum @alias Loop Condition
typedef enum vm_for_cmp_e {
  VM_FOR_CMP_LT = 0,
  VM_FOR_CMP_LE,
  VM_FOR_CMP_GT,
  VM_FOR_CMP_GE,
  VM_FOR_CMP_CNT
} vm_for_cmp_e;

typedef struct vm_for_code_t {
  vm_span_t span;      // Block ids [start, end) this loop runs; start = own id + 1
  float k_start;       // Start when input 0 is unwired
  float k_end;         // End when input 1 is unwired
  float k_step;        // Step when input 2 is unwired
  uint16_t max_turns;  // Turn budget per pass
  uint8_t op;          // How the iterator advances @enum-ref vm_for_op_e
  uint8_t cmp;         // Loop condition @enum-ref vm_for_cmp_e
  uint8_t rt;          // @runtime
  uint8_t _pad[3];
} vm_for_code_t;

_Static_assert(offsetof(vm_for_code_t, span) == 0, "vm_block_span() reads the span off custom_data head");
_Static_assert(offsetof(vm_for_code_t, k_start) % 4 == 0, "literals must stay 4-aligned");
_Static_assert(sizeof(vm_for_code_t) == 24, "wire format header size");
#define VM_FOR_CUSTOM_LEN sizeof(vm_for_code_t)

#define VM_FOR_IN_START 0u
#define VM_FOR_IN_END 1u
#define VM_FOR_IN_STEP 2u

#define VM_FOR_RT_BAD 0x01u
#define VM_FOR_BAD_CAPPED 0u
#define VM_FOR_BAD_NOT_FINITE 1u

static inline void vm_for_bad_loop(vm_block_h b, vm_for_code_t* c, uint32_t turns, uint8_t reason) {
  vm_block_mark_failed(b);
  if (c->rt & VM_FOR_RT_BAD) return;
  c->rt |= VM_FOR_RT_BAD;
  vm_block_report_error(VM_BLK_ERR_NEW(ERR_VM_FOR_BAD_LOOP, .block_idx = b->cfg.block_idx, .turns = turns, .cap = c->max_turns,
                  .reason = reason), b);
}

static inline bool vm_for_keep_going(uint8_t cmp, float i, float end) {
  switch (cmp) {
    case VM_FOR_CMP_LT: return i < end;
    case VM_FOR_CMP_LE: return i <= end;
    case VM_FOR_CMP_GT: return i > end;
    default: return i >= end;
  }
}

static inline bool vm_verify_for(vm_block_h b) {
  const vm_for_code_t* c = (const vm_for_code_t*)vm_block_get_custom_data(b);
  return c->op < VM_FOR_OP_CNT && c->cmp < VM_FOR_CMP_CNT;
}

static inline void vm_blk_for(vm_block_h b) {
  const vm_span_t* sp = vm_block_get_span(b);
  const vm_span_t range = sp ? *sp : (vm_span_t){0, 0};
  vm_block_claim_span(b, range.start, range.end);

  const bool owned = (b->cfg.rt & VM_BLK_RT_SPAN) != 0;

  vm_for_code_t* c = (vm_for_code_t*)vm_block_get_custom_data(b);

  float i = 0.0f, end = 0.0f, step = 0.0f;
  bool go = owned;
  IF_BLOCK_ENABLED(b) {
    go = go && vm_block_check(b, VM_BLOCK_GET_PARAM(i, b, VM_FOR_IN_START, c->k_start));
    go = go && vm_block_check(b, VM_BLOCK_GET_PARAM(end, b, VM_FOR_IN_END, c->k_end));
    go = go && vm_block_check(b, VM_BLOCK_GET_PARAM(step, b, VM_FOR_IN_STEP, c->k_step));
    go = go && isfinite(i) && isfinite(end) && isfinite(step);
  } else {
    go = false;
  }

  if (!go || !vm_for_keep_going(c->cmp, i, end)) {
    vm_block_set_eno(b, false);
    return;
  }

  vm_block_set_eno(b, true);

  vm_obj_h idx = (b->cfg.q_cnt >= 1) ? vm_block_get_outputs(b)[0] : NULL;
  const uint32_t budget = c->max_turns;
  uint32_t turns = 0;

  while (vm_for_keep_going(c->cmp, i, end)) {
    if (unlikely(turns >= budget)) {
      vm_for_bad_loop(b, c, turns, VM_FOR_BAD_CAPPED);
      return;
    }

    if (idx) {
      err_h e = VM_OBJ_SET_SCALAR_AT_IDX(i, idx, 0);
      if (unlikely(e)) {
        vm_block_report_error(e, b);
        return;
      }
    }

    vm_exec_run_range(range.start, range.end);
    if (vm_exec_cancelled()) return;
    turns++;

    switch (c->op) {
      case VM_FOR_OP_ADD: i += step; break;
      case VM_FOR_OP_SUB: i -= step; break;
      case VM_FOR_OP_MUL: i *= step; break;
      default:            i /= step; break;
    }
    if (unlikely(!isfinite(i))) {
      vm_for_bad_loop(b, c, turns, VM_FOR_BAD_NOT_FINITE);
      return;
    }
  }

  c->rt &= (uint8_t)~VM_FOR_RT_BAD;
}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_FOR @title For @category flow @state vm_for_code_t
//@block-description Runs the blocks in its span repeatedly within one pass: for (i = start; i <cmp> end; i = i <op> step), bounded by max_turns.
//@in 0 start @title Start
//@in 1 end @title End
//@in 2 step @title Step
//@out 0 index @title Index @description The iterator, published before each turn.
#define VM_BLOCK_TYPE_FOR \
  {.run = vm_blk_for, .check = vm_verify_for, .min_in = 0, .min_q = 0, .required_in = 0x0u, .state_len = VM_FOR_CUSTOM_LEN}
