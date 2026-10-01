#include "vm_block_for.h"

static void vm_for_bad_loop(vm_block_h b, vm_for_code_t* c, uint32_t turns, uint8_t reason) {
  vm_block_mark_failed(b);
  if (c->rt & VM_FOR_RT_BAD) return;
  c->rt |= VM_FOR_RT_BAD;
  vm_block_report_error(VM_BLK_ERR_NEW(ERR_VM_FOR_BAD_LOOP, .block_idx = b->cfg.block_idx, .turns = turns, .cap = c->max_turns,
                  .reason = reason), b);
}

static bool vm_for_keep_going(uint8_t cmp, float i, float end) {
  switch (cmp) {
    case VM_FOR_CMP_LT: return i < end;
    case VM_FOR_CMP_LE: return i <= end;
    case VM_FOR_CMP_GT: return i > end;
    default: return i >= end;
  }
}

bool vm_verify_for(vm_block_h b) {
  const vm_for_code_t* c = (const vm_for_code_t*)vm_block_get_custom_data(b);
  return c->op < VM_FOR_OP_CNT && c->cmp < VM_FOR_CMP_CNT;
}

void vm_blk_for(vm_block_h b) {
  const vm_span_t* sp = vm_block_get_span(b);
  const vm_span_t range = sp ? *sp : (vm_span_t){0, 0};
  vm_block_claim_span(b, range.start, range.end);

  const bool owned = (b->cfg.rt & VM_BLK_RT_SPAN) != 0;

  vm_for_code_t* c = (vm_for_code_t*)vm_block_get_custom_data(b);

  float i = 0.0f, end = 0.0f, step = 0.0f;
  bool go = owned;
  IF_BLOCK_ENABLED(b) {
    go = go && vm_block_check(b, VM_BLOCK_GET_PARAM(i, b, VM_IN_FOR_START, c->k_start));
    go = go && vm_block_check(b, VM_BLOCK_GET_PARAM(end, b, VM_IN_FOR_END, c->k_end));
    go = go && vm_block_check(b, VM_BLOCK_GET_PARAM(step, b, VM_IN_FOR_STEP, c->k_step));
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
