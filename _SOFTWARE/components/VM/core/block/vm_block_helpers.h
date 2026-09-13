#pragma once
#include "vm_block.h"

/* Shared block plumbing. Configuration faults are sticky diagnostics, but
 * remain execution faults on every invocation. Helpers never choose ENO. */
static inline bool vm_block_check(vm_block_h b, err_h e) {
  if (!e) return true;
  vm_block_report_error(e, b->cfg.block_idx, b->cfg.block_type);
  return false;
}

static inline bool vm_block_cfg_bad(vm_block_h b) {
  g_vm_block_fault = true;
  if (!(b->cfg.rt & VM_BLK_RT_CFG_BAD)) {
    b->cfg.rt |= VM_BLK_RT_CFG_BAD;
    vm_block_report_error(VM_BLK_ERR_NEW(ERR_VM_BLK_BAD_SHAPE, .blk_id = b->cfg.block_idx, .in_cnt = b->cfg.in_cnt, .q_cnt = b->cfg.q_cnt), b->cfg.block_idx, b->cfg.block_type);
  }
  return false;
}

/* Build-time predicate: the builder returns the diagnostic to its caller.
 * Runtime vm_block_require owns reporting and sticky fault state instead. */
static inline bool vm_block_shape_valid(vm_block_h b, uint8_t min_in, uint8_t min_q, uint16_t required_inputs) {
  if (b->cfg.in_cnt < min_in || b->cfg.q_cnt < min_q) return false;
  for (uint8_t pin = 0; pin < VM_BLOCK_MAX_IN; ++pin) {
    if (!(required_inputs & (1u << pin))) continue;
    if (pin >= b->cfg.in_cnt || !vm_block_get_inputs(b)[pin]) return false;
  }
  return true;
}

static inline bool vm_block_require(vm_block_h b, uint8_t min_in, uint8_t min_q, uint16_t required_inputs) {
  if (b->cfg.in_cnt < min_in || b->cfg.q_cnt < min_q) return vm_block_cfg_bad(b);
  for (uint8_t pin = 0; pin < VM_BLOCK_MAX_IN; ++pin) {
    if (!(required_inputs & (1u << pin))) continue;
    if (pin >= b->cfg.in_cnt) return vm_block_cfg_bad(b);
    if (vm_block_get_inputs(b)[pin]) continue;
    g_vm_block_fault = true;
    if (!(b->cfg.rt & VM_BLK_RT_CFG_BAD)) {
      b->cfg.rt |= VM_BLK_RT_CFG_BAD;
      vm_block_report_error(vm_block_err_pin_unlinked(b->cfg.block_idx, pin, false), b->cfg.block_idx, b->cfg.block_type);
    }
    return false;
  }
  return true;
}

/* Absence is a valid fallback, never an error-producing required-pin lookup. */
static inline const vm_accessor_t* vm_block_optional_in(vm_block_h b, uint8_t pin) {
  return pin < b->cfg.in_cnt ? vm_block_get_inputs(b)[pin] : NULL;
}

/** @brief Optional pin read. Unwired/out-of-range pins store fallback and
 * return NULL; connected failures preserve output and return err_h.
 * Call vm_block_check at the execution boundary to report the error. */
#define VM_BLOCK_GET_PARAM(output, b, pin, fallback)                  \
  ({                                                                  \
    const vm_accessor_t* __bp_acc = vm_block_optional_in((b), (pin)); \
    err_h                __bp_err = NULL;                             \
    if (__bp_acc)                                                     \
      __bp_err = VM_OBJ_SCALAR_GET((output), __bp_acc);               \
    else                                                              \
      (output) = (fallback);                                          \
    __bp_err;                                                         \
  })

/* Gate false is quiet; ordinary value writes (including false) remain loud. */
static inline void vm_block_drive_gate(vm_block_h b, uint8_t pin, bool state) {
  if (pin >= b->cfg.q_cnt) return;
  vm_obj_h q = vm_block_get_outputs(b)[pin];
  if (!state)
    vm_block_check(b, vm_obj_clear_quiet(q));
  else {
    uint8_t one = 1;
    BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(one, q, 0), b);
  }
}


// User Mutation Boundary (vm_block_obj_*)
/* User mutation boundary. Protected objects remain readable; internal producer
   APIs above still honor mutable. A protection check applies to each object
   actually written, not to containers merely traversed by an accessor. */
err_h vm_block_obj_copy_content(const vm_accessor_t* source, const vm_accessor_t* target);
err_h vm_block_obj_copy_direct(vm_obj_h src, vm_obj_h dst);

#define VM_BLOCK_OBJ_COPY_CONTENT(source, target) \
  _Generic((source),                               \
      vm_obj_h: vm_block_obj_copy_direct,         \
      default:  vm_block_obj_copy_content)((source), (target))

err_h vm_block_obj_clone_into(const vm_accessor_t* source, const vm_accessor_t* target);
err_h vm_block_obj_link(const vm_accessor_t* child, const vm_accessor_t* target);
err_h vm_block_obj_mark_updated(vm_obj_h obj);
