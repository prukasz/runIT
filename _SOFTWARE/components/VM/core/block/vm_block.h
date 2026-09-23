#pragma once
#include <sdkconfig.h>
#include "vm_obj_access.h"

/** @brief Sentinel for unwired pin or absent ENO. */
#define VM_BLOCK_NO_ID 0xFFFFu
/*
 * VM Block Execution Model & API
 *
 * Defines the execution block descriptor, memory layout, slice getters,
 * enable evaluation, execution span takeover, and block error reporting.
 *
 * Linear block memory layout in VM store:
 *   [cfg (16B)][in_cnt * const vm_accessor_t*][q_cnt * vm_obj_h][en_cnt * const vm_accessor_t*][custom_data bytes]
 *
 * Logic Flow:
 *   1. Constants & Bitflags (Pin limits, Enable modes, Error policies, Runtime flags)
 *   2. Types & Block Data Structure (vm_span_t, vm_block_data_t, vm_block_h, vm_block_fn)
 *   3. Sizing & Registry Lookups (vm_block_get_by_id, vm_block_calc_size, vm_block_get_total_size, vm_block_get_custom_len)
 *   4. Slice Accessors (vm_block_get_inputs, vm_block_get_outputs, vm_block_get_en_list, vm_block_get_custom_data)
 *   5. Pin Access & Validation (vm_block_get_in, vm_block_get_out)
 *   6. Runtime Evaluation, Spans & ENO (vm_block_is_enabled, vm_block_set_eno, vm_block_triggered, vm_block_input_fresh, vm_block_get_span, vm_block_claim_span)
 *   7. Error Reporting & Execution Macros (vm_block_report_error, BLOCK_CALL, IF_BLOCK_*)
 */

// ===========================================================================
// 1. Constants & Bitflags
// ===========================================================================

/** @brief Enable evaluation mode across en_cnt sources. */
#define VM_BLK_EN_ANY 0x00u  // OR / branch merge
#define VM_BLK_EN_ALL 0x01u  // AND / all conditions met

/** @brief Error policy on body failure. */
#define VM_BLK_ERR_STOP     0x00u  // Publish false ENO; downstream skips
#define VM_BLK_ERR_CONTINUE 0x01u  // Report failure and continue execution

/** @brief Runtime status flags (latched per pass or sticky across load). */
#define VM_BLK_RT_TRIGGERED (1u << 0u)  // Fresh data arrived on an input (latched by vm_block_triggered)
#define VM_BLK_RT_SPAN      (1u << 1u)  // Block claimed an execution range (vm_block_claim_span)
#define VM_BLK_RT_FAULT     (1u << 2u)  // This call failed (vm_block_report_error / vm_block_mark_failed); read by on_error
#define VM_BLK_RT_CFG_BAD   (1u << 6u)  // Sticky: malformed custom_data reported once per load
#define VM_BLK_RT_SPAN_BAD  (1u << 7u)  // Sticky: malformed span reported once per load

#define VM_BLK_RT_PER_CALL (VM_BLK_RT_TRIGGERED | VM_BLK_RT_SPAN | VM_BLK_RT_FAULT)

// ===========================================================================
// 2. Types & Block Data Structure
// ===========================================================================

/**
 * @brief Execution order span [start, end) for loop and span owner blocks.
 */
typedef struct vm_span_t {
  uint16_t start;
  uint16_t end;
} vm_span_t;

/**
 * @brief Block descriptor header (16 bytes) followed by flexible trailing arrays.
 */
typedef struct vm_block_data_t {
  struct {
    uint16_t block_idx;   // [0..1]   Visual block identifier
    uint8_t  block_type;  // [2]      Palette type index
    uint8_t  in_cnt;      // [3]      Input count
    uint8_t  q_cnt;       // [4]      Output count
    uint8_t  en_cnt;      // [5]      Enable count (0 = root, always enabled)
    uint8_t  en_mode;     // [6]      VM_BLK_EN_ANY / _ALL
    uint8_t  on_error;    // [7]      VM_BLK_ERR_STOP / _CONTINUE
    uint16_t custom_len;  // [8..9]   Private custom_data byte length
    uint8_t  rt;          // [10]     Runtime status bits (VM_BLK_RT_*)
    vm_obj_h eno;         // [12..15] Output ENO object (NULL if none)
  } cfg;
  uint8_t data[];
} vm_block_data_t;

_Static_assert(sizeof(struct vm_block_data_t) == 16, "custom_len and rt must stay inside alignment padding before eno");

typedef vm_block_data_t* vm_block_h;

/**
 * @brief Block execution handler signature (the run member of a vm_block_type_t palette entry).
 */
typedef void (*vm_block_fn)(vm_block_h);

/**
 * @brief Extra load-time check of a block's private state (enum ranges,
 * cross-field rules). The shape itself is checked from vm_block_type_t.
 */
typedef bool (*vm_block_verify_fn)(vm_block_h);

/**
 * @brief One palette entry: everything the VM knows about a block type.
 *
 * Indexed by block_type in g_vm_block_types[] (blocks/vm_blocks_table.c). Each
 * block header defines its entry as VM_BLOCK_TYPE_<NAME>, next to the body and
 * the state layout it describes. vm_block_verify() checks a built block against
 * it once at load, so bodies never re-check their own shape.
 */
typedef struct vm_block_type_t {
  vm_block_fn run;           // Body, called every pass (NULL: type not in the palette)
  vm_block_verify_fn check;  // Extra private-state check at load; NULL = none
  uint8_t min_in;            // in_cnt >= min_in
  uint8_t min_q;             // q_cnt >= min_q
  uint16_t required_in;      // Bit n: input n must be wired
  uint16_t state_len;        // custom_len >= state_len
} vm_block_type_t;

// ===========================================================================
// 3. Sizing & Registry Lookups
// ===========================================================================

/**
 * @brief Retrieve block handle from registry by ID (NULL if out of range).
 */
static inline vm_block_h vm_block_get_by_id(uint16_t id) {
  return (vm_block_h)vm_store_get(VM_REG_BLK, id);
}

/**
 * @brief Compute total allocation bytes required for one block of the given shape.
 */
static inline size_t vm_block_calc_size(uint8_t in_cnt, uint8_t q_cnt, uint8_t en_cnt, uint16_t custom_len) {
  return sizeof(vm_block_data_t) + (size_t)in_cnt * sizeof(const vm_accessor_t*) + (size_t)q_cnt * sizeof(vm_obj_h) + (size_t)en_cnt * sizeof(const vm_accessor_t*) + custom_len;
}

/**
 * @brief Return total allocated size of block in bytes.
 */
static inline size_t vm_block_get_total_size(vm_block_h b) {
  return vm_block_calc_size(b->cfg.in_cnt, b->cfg.q_cnt, b->cfg.en_cnt, b->cfg.custom_len);
}

/**
 * @brief Return custom data length in bytes.
 */
static inline uint16_t vm_block_get_custom_len(vm_block_h b) {
  return b->cfg.custom_len;
}

// ===========================================================================
// 4. Slice Accessors
// ===========================================================================

/**
 * @brief Pointer to array of input accessors (in_cnt entries).
 */
static inline const vm_accessor_t** vm_block_get_inputs(vm_block_h b) {
  return (const vm_accessor_t**)b->data;
}

/**
 * @brief Pointer to array of output object handles (q_cnt entries).
 */
static inline vm_obj_h* vm_block_get_outputs(vm_block_h b) {
  return (vm_obj_h*)(vm_block_get_inputs(b) + b->cfg.in_cnt);
}

/**
 * @brief Pointer to array of enable accessors (en_cnt entries).
 */
static inline const vm_accessor_t** vm_block_get_en_list(vm_block_h b) {
  return (const vm_accessor_t**)(vm_block_get_outputs(b) + b->cfg.q_cnt);
}

/**
 * @brief Pointer to private custom data buffer directly following en_list.
 */
static inline void* vm_block_get_custom_data(vm_block_h b) {
  return (void*)(vm_block_get_en_list(b) + b->cfg.en_cnt);
}

// ===========================================================================
// 5. Pin Access & Validation
// Note: vm_block_get_in and vm_block_get_out perform runtime validation for
// diagnostics and bring-up code. Production block handlers access resolved
// pin arrays directly via vm_block_get_inputs / vm_block_get_outputs for speed.
// ===========================================================================

/**
 * @brief Fetch input pin accessor with bounds and linkage validation.
 * @param[out] target Receives accessor handle.
 * @param[in]  b      Block handle.
 * @param[in]  id     Input pin index (0 .. in_cnt-1).
 * @return err_h NULL on success, ERR_VM_BLOCK_PIN_MISSING or ERR_VM_BLOCK_PIN_UNLINKED.
 */
static inline SE_MUST_USE err_h vm_block_get_in(const vm_accessor_t** target, vm_block_h b, uint8_t id) {
  if (unlikely(id >= b->cfg.in_cnt)) return vm_block_err_pin_missing(b->cfg.block_idx, id, false);
  const vm_accessor_t* acc = vm_block_get_inputs(b)[id];
  if (unlikely(!acc)) return vm_block_err_pin_unlinked(b->cfg.block_idx, id, false);
  *target = acc;
  return NULL;
}

/**
 * @brief Fetch output pin object with bounds and linkage validation.
 * @param[out] target Receives object handle.
 * @param[in]  b      Block handle.
 * @param[in]  id     Output pin index (0 .. q_cnt-1).
 * @return err_h NULL on success, ERR_VM_BLOCK_PIN_MISSING or ERR_VM_BLOCK_PIN_UNLINKED.
 */
static inline SE_MUST_USE err_h vm_block_get_out(vm_obj_h* target, vm_block_h b, uint8_t id) {
  if (unlikely(id >= b->cfg.q_cnt)) return vm_block_err_pin_missing(b->cfg.block_idx, id, true);
  vm_obj_h obj = vm_block_get_outputs(b)[id];
  if (unlikely(!obj)) return vm_block_err_pin_unlinked(b->cfg.block_idx, id, true);
  *target = obj;
  return NULL;
}

// ===========================================================================
// 6. Runtime Evaluation, Spans & ENO
// ===========================================================================

/**
 * @brief Mark this call of @p b as failed without reporting (the error was
 * reported elsewhere, or is a latched standing condition).
 *
 * The fault is a per-call bit in the block's own cfg.rt, cleared before every
 * call, so a span owner and the blocks in its body each keep their own.
 */
static inline void vm_block_mark_failed(vm_block_h b) {
  b->cfg.rt |= VM_BLK_RT_FAULT;
}

/** @brief True when this call of @p b has failed so far (cfg.on_error acts on it). */
static inline bool vm_block_failed(vm_block_h b) {
  return (b->cfg.rt & VM_BLK_RT_FAULT) != 0;
}

/** @brief Reports @p cause wrapped with the block's identity and marks this call failed. */
void vm_block_report_error(err_h cause, vm_block_h b);

/**
 * @brief Evaluates block enable status across en_cnt sources (0 = always enabled).
 */
static inline bool vm_block_is_enabled(vm_block_h b) {
  // if no en inputs = active
  uint8_t n = b->cfg.en_cnt;
  if (n == 0) return true;

  const vm_accessor_t** en  = vm_block_get_en_list(b);
  const bool            all = (b->cfg.en_mode == VM_BLK_EN_ALL);
  for (uint8_t i = 0; i < n; i++) {
    bool  v = false;
    err_h e = VM_OBJ_SCALAR_GET(v, en[i]);
    if (unlikely(e)) {
      vm_block_report_error(e, b);
      v = false;  // fail closed
    }
    // check if one required or all
    if (all) {
      if (!v) return false;
    } else if (v) {
      return true;
    }
  }
  return all;
}

/**
 * @brief Set block ENO: true marks updated (loud), false clears quietly without upd.
 */
static inline void vm_block_set_eno(vm_block_h b, bool state) {
  if (!b->cfg.eno) return;
  *(uint8_t*)b->cfg.eno->payload = state ? 1 : 0;
  if (state) {
    b->cfg.eno->head.f.upd = 1;
  }
}

/** @brief True if any input carries fresh data (latches VM_BLK_RT_TRIGGERED).
 */
bool vm_block_triggered(vm_block_h b);

/**
 * @brief Freshness (`upd`) belongs to the owning object; unresolved pins fail closed silently.
 */
static inline bool vm_block_pin_fresh(const vm_accessor_t* acc) {
  if (!acc) return false;

  if (likely(acc->flags & VM_ACC_F_CACHED)) {
    return acc->c_payload.owner && acc->c_payload.owner->head.f.upd;
  }

  vm_obj_payload_t r;
  if (vm_acc_resolve_fast(acc, &r)) {
    return r.owner && r.owner->head.f.upd;
  }

  vm_obj_h o = NULL;
  err_h error = vm_obj_get_owner(&o, acc);
  if (error) { SE_release(error); return false; }
  if (!o) return false;
  return o->head.f.upd != 0;
}

/**
 * @brief True if specific input pin carries fresh data.
 */
static inline bool vm_block_input_fresh(vm_block_h b, uint8_t pin) {
  if (unlikely(pin >= b->cfg.in_cnt)) return false;
  return vm_block_pin_fresh(vm_block_get_inputs(b)[pin]);
}

/** @brief Trigger from one source pin, ignoring destination/parameter inputs.
 */
static inline bool vm_block_triggered_by(vm_block_h b, uint8_t pin) {
  if (!vm_block_input_fresh(b, pin)) return false;
  b->cfg.rt |= VM_BLK_RT_TRIGGERED;
  return true;
}

/**
 * @brief Block execution span from custom_data (NULL if custom_len < sizeof(vm_span_t)).
 */
static inline const vm_span_t* vm_block_get_span(vm_block_h b) {
  if (b->cfg.custom_len < sizeof(vm_span_t)) return NULL;
  return (const vm_span_t*)vm_block_get_custom_data(b);
}

/**
 * @brief Takes over [start, end) range so outer execution walk jumps over it.
 */
void vm_block_claim_span(vm_block_h b, uint16_t start, uint16_t end);

// ===========================================================================
// 7. Error Reporting & Execution Macros
// ===========================================================================

/* Block activation macros. Nestable: IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b) { ... }
 */
#define IF_BLOCK_ENABLED(block)   if (vm_block_is_enabled(block))
#define IF_BLOCK_TRIGGERED(block) if (vm_block_triggered(block))

/** @brief Runs an err_h call, reporting failure with block context and marking the call failed.
 */
#define BLOCK_CALL(call, block)                                                       \
  do {                                                                                \
    err_h __bc_e = (call);                                                            \
    if (__bc_e) {                                                                     \
      vm_block_report_error(__bc_e, (block)); \
    }                                                                                 \
  } while (0)

#define VM_BLK_ERR_NEW(tag_name, ...) SE_ERR_NEW_OWNED(OWNER_VM_BLOCK, tag_name, __VA_ARGS__)

#define VM_BLK_EMIT_ERR(tag_name, ...) SE_push_to_handler(VM_BLK_ERR_NEW(tag_name, __VA_ARGS__))
