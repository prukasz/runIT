#pragma once

#include "vm_err.h"

#include "errors.h"

/* Errors of the VM core (module ERR_MOD_VM). Tag numbers are new in this tree; never renumber, append at the end.
 * Payloads are limited to ERR_DATA_MAX bytes. `obj_id` / `parent_id` carry the failing object ID (or
 * VM_OBJ_ID_DYN_BIT | dyn_id, or VM_OBJ_ID_NONE) for vm_obj_lookup_by_id(). */

// clang-format off
#define ERR_VM_MAP(X) \
  X(ERR_VM_ALLOC_EXHAUSTED, ERR_TAG(ERR_MOD_VM, 1), ERR_SEV_HIGH, { uint32_t requested; uint32_t remaining; }) \
  X(ERR_VM_ACCESSOR_UNKNOWN_ID, ERR_TAG(ERR_MOD_VM, 2), ERR_SEV_LOW, { uint16_t id; }) \
  X(ERR_VM_ACCESSOR_OOB, ERR_TAG(ERR_MOD_VM, 3), ERR_SEV_LOW, { uint16_t id; uint8_t chain_pos; uint16_t index; uint16_t obj_id; }) \
  X(ERR_VM_ACCESSOR_TYPE_MISMATCH, ERR_TAG(ERR_MOD_VM, 4), ERR_SEV_LOW, { uint16_t id; uint8_t chain_pos; uint8_t expected; /*@enum-ref vm_obj_t_e*/ uint8_t actual; /*@enum-ref vm_obj_t_e*/ uint16_t obj_id; }) \
  X(ERR_VM_ACCESSOR_NULL_OBJ, ERR_TAG(ERR_MOD_VM, 5), ERR_SEV_LOW, { uint16_t id; uint8_t chain_pos; uint16_t parent_id; }) \
  X(ERR_VM_ACCESSOR_DEPTH_EXCEEDED, ERR_TAG(ERR_MOD_VM, 6), ERR_SEV_LOW, { uint16_t id; }) \
  X(ERR_VM_ACCESSOR_INDEX_FAILED, ERR_TAG(ERR_MOD_VM, 7), ERR_SEV_LOW, { uint16_t id; uint8_t chain_pos; }) \
  X(ERR_VM_ACCESSOR_NAME_NOT_FOUND, ERR_TAG(ERR_MOD_VM, 8), ERR_SEV_LOW, { uint16_t id; uint8_t chain_pos; char name[16]; }) \
  X(ERR_VM_ACCESSOR_NOT_MUTABLE, ERR_TAG(ERR_MOD_VM, 9), ERR_SEV_LOW, { uint16_t id; uint8_t chain_pos; uint16_t obj_id; }) \
  X(ERR_VM_BLOCK_PIN_MISSING, ERR_TAG(ERR_MOD_VM, 10), ERR_SEV_LOW, { uint16_t block_idx; uint8_t pin_id; uint8_t is_out; }) \
  X(ERR_VM_BLOCK_PIN_UNLINKED, ERR_TAG(ERR_MOD_VM, 11), ERR_SEV_LOW, { uint16_t block_idx; uint8_t pin_id; uint8_t is_out; }) \
  X(ERR_VM_BLOCK_FAILED, ERR_TAG(ERR_MOD_VM, 12), ERR_SEV_MEDIUM, { uint16_t block_idx; uint8_t block_type; /*@id vm-block-type*/ }) \
  X(ERR_VM_OBJ_COPY_MISMATCH, ERR_TAG(ERR_MOD_VM, 13), ERR_SEV_LOW, { uint8_t src_type; /*@enum-ref vm_obj_t_e*/ uint8_t dst_type; /*@enum-ref vm_obj_t_e*/ uint16_t src_size; uint16_t dst_size; }) \
  X(ERR_VM_OBJ_COPY_SHAPE, ERR_TAG(ERR_MOD_VM, 14), ERR_SEV_LOW, { uint16_t index; uint8_t depth; uint8_t reason; }) \
  X(ERR_VM_OBJ_NOT_MUTABLE, ERR_TAG(ERR_MOD_VM, 15), ERR_SEV_LOW, { uint16_t obj_id; }) \
  X(ERR_VM_OBJ_OOB, ERR_TAG(ERR_MOD_VM, 16), ERR_SEV_LOW, { uint16_t index; uint16_t obj_id; }) \
  X(ERR_VM_OBJ_NOT_PTR, ERR_TAG(ERR_MOD_VM, 17), ERR_SEV_LOW, { uint8_t actual; /*@enum-ref vm_obj_t_e*/ uint16_t obj_id; }) \
  X(ERR_VM_OBJ_BAD_TYPE, ERR_TAG(ERR_MOD_VM, 18), ERR_SEV_LOW, { uint8_t type; /*@enum-ref vm_obj_t_e*/ }) \
  X(ERR_VM_OBJ_EMPTY, ERR_TAG(ERR_MOD_VM, 19), ERR_SEV_LOW, { uint8_t type; /*@enum-ref vm_obj_t_e*/ }) \
  X(ERR_VM_OBJ_BAD_SIZE, ERR_TAG(ERR_MOD_VM, 20), ERR_SEV_LOW, { uint8_t type; /*@enum-ref vm_obj_t_e*/ uint16_t payload_size; uint8_t width; }) \
  X(ERR_VM_OBJ_NAME_TOO_LONG, ERR_TAG(ERR_MOD_VM, 21), ERR_SEV_LOW, { uint8_t len; }) \
  X(ERR_VM_OBJ_RETENTIVE_PTR, ERR_TAG(ERR_MOD_VM, 22), ERR_SEV_LOW, { uint8_t type; /*@enum-ref vm_obj_t_e*/ }) \
  X(ERR_VM_LOAD_BAD_STATE, ERR_TAG(ERR_MOD_VM, 23), ERR_SEV_LOW, { uint8_t state; uint8_t expected; }) \
  X(ERR_VM_LOAD_TOO_BIG, ERR_TAG(ERR_MOD_VM, 24), ERR_SEV_LOW, { uint32_t requested; uint32_t available; }) \
  X(ERR_VM_LOAD_DATA_RANGE, ERR_TAG(ERR_MOD_VM, 25), ERR_SEV_LOW, { uint16_t id; uint16_t start_idx; uint16_t len; uint16_t items; }) \
  X(ERR_VM_LOAD_SHORT_RECORD, ERR_TAG(ERR_MOD_VM, 26), ERR_SEV_LOW, { uint8_t packet; uint16_t need; uint16_t got; }) \
  X(ERR_VM_ACC_INDEX_OOB, ERR_TAG(ERR_MOD_VM, 27), ERR_SEV_LOW, { uint16_t acc_id; uint8_t pos; uint8_t count; }) \
  X(ERR_VM_ACC_BAD_KIND, ERR_TAG(ERR_MOD_VM, 28), ERR_SEV_LOW, { uint16_t acc_id; uint8_t pos; uint8_t kind; /*@enum-ref vm_index_kind_e*/ }) \
  X(ERR_VM_REG_OOB, ERR_TAG(ERR_MOD_VM, 29), ERR_SEV_LOW, { uint8_t kind; uint16_t id; uint16_t count; }) \
  X(ERR_VM_REG_DUP, ERR_TAG(ERR_MOD_VM, 30), ERR_SEV_LOW, { uint8_t kind; uint16_t id; }) \
  X(ERR_VM_BLK_BAD_SHAPE, ERR_TAG(ERR_MOD_VM, 31), ERR_SEV_LOW, { uint16_t blk_id; uint8_t in_cnt; uint8_t q_cnt; }) \
  X(ERR_VM_BLK_BAD_REF, ERR_TAG(ERR_MOD_VM, 32), ERR_SEV_LOW, { uint16_t blk_id; uint16_t ref_id; uint8_t slot; uint8_t kind; }) \
  X(ERR_VM_DYN_FULL, ERR_TAG(ERR_MOD_VM, 33), ERR_SEV_LOW, { uint16_t limit; }) \
  X(ERR_VM_BLK_UNKNOWN_TYPE, ERR_TAG(ERR_MOD_VM, 34), ERR_SEV_LOW, { uint16_t blk_id; uint8_t block_type; /*@id vm-block-type*/ }) \
  X(ERR_VM_EXEC_BLOCK_HUNG, ERR_TAG(ERR_MOD_VM, 35), ERR_SEV_HIGH, { uint16_t block_idx; uint16_t ms; }) \
  X(ERR_VM_EXEC_SPAN_DEPTH, ERR_TAG(ERR_MOD_VM, 36), ERR_SEV_LOW, { uint16_t block_idx; uint8_t depth; }) \
  X(ERR_VM_EXEC_BAD_SPAN, ERR_TAG(ERR_MOD_VM, 37), ERR_SEV_LOW, { uint16_t block_idx; uint16_t start; uint16_t end; }) \
  X(ERR_VM_EVENT_OVERFLOW, ERR_TAG(ERR_MOD_VM, 38), ERR_SEV_MEDIUM, { uint16_t source; uint16_t depth; uint16_t dropped; }) \
  X(ERR_VM_EXPR_BAD_CODE, ERR_TAG(ERR_MOD_VM, 39), ERR_SEV_LOW, { uint16_t block_idx; uint16_t pc; uint8_t opcode; /*@enum-ref vm_expr_op_e*/ uint8_t reason; }) \
  X(ERR_VM_EXPR_MATH, ERR_TAG(ERR_MOD_VM, 40), ERR_SEV_LOW, { uint16_t block_idx; uint16_t pc; uint8_t opcode; /*@enum-ref vm_expr_op_e*/ uint8_t reason; }) \
  X(ERR_VM_FOR_BAD_LOOP, ERR_TAG(ERR_MOD_VM, 41), ERR_SEV_LOW, { uint16_t block_idx; uint32_t turns; uint16_t cap; uint8_t reason; }) \
  X(ERR_VM_OBJ_OWNERSHIP, ERR_TAG(ERR_MOD_VM, 42), ERR_SEV_LOW, { uint8_t reason; uint8_t limit; }) \
  X(ERR_VM_OBJ_USR_PROTECTED, ERR_TAG(ERR_MOD_VM, 43), ERR_SEV_LOW, { uint16_t obj_id; }) \
  X(ERR_VM_LOAD_RUNNING, ERR_TAG(ERR_MOD_VM, 44), ERR_SEV_LOW, { uint8_t mode; }) \
  X(ERR_VM_EXEC_CONTROL, ERR_TAG(ERR_MOD_VM, 45), ERR_SEV_LOW, { uint8_t command; /*@enum-ref vm_exec_command_e*/ uint8_t mode; }) \
  X(ERR_VM_OVERRIDE_PTR_UNSUPPORTED, ERR_TAG(ERR_MOD_VM, 46), ERR_SEV_LOW, { uint16_t obj_id; }) \
  X(ERR_VM_OVERRIDE_BAD_RECORD, ERR_TAG(ERR_MOD_VM, 47), ERR_SEV_LOW, { uint16_t obj_id; uint16_t declared_len; uint16_t item_size; }) \
  X(ERR_VM_EXEC_SELF_BARRIER, ERR_TAG(ERR_MOD_VM, 48), ERR_SEV_LOW, { uint8_t operation; }) \
  X(ERR_VM_EXEC_FAULT_LATCHED, ERR_TAG(ERR_MOD_VM, 49), ERR_SEV_CRITICAL, { uint16_t device_id; /*@id device*/ uint32_t root_tag; /*@id error-tag*/ }) \
  X(ERR_VM_PERIODIC_OVERRUN, ERR_TAG(ERR_MOD_VM, 50), ERR_SEV_LOW, { uint16_t block_idx; uint32_t missed; uint32_t period_ms; }) \
  X(ERR_VM_RETAIN_UNNAMED, ERR_TAG(ERR_MOD_VM, 51), ERR_SEV_LOW, { uint16_t obj_id; }) \
  X(ERR_VM_RETAIN_TOO_BIG, ERR_TAG(ERR_MOD_VM, 52), ERR_SEV_LOW, { uint16_t obj_id; uint32_t need; uint32_t max; }) \
  X(ERR_VM_RETAIN_MISMATCH, ERR_TAG(ERR_MOD_VM, 53), ERR_SEV_LOW, { uint16_t obj_id; uint8_t type; /*@enum-ref vm_obj_t_e*/ uint16_t size; }) \
  X(ERR_VM_RETAIN_CORRUPT, ERR_TAG(ERR_MOD_VM, 54), ERR_SEV_MEDIUM, { uint32_t offset; }) \
  X(ERR_VM_SUB_TRACK_FULL, ERR_TAG(ERR_MOD_VM, 55), ERR_SEV_LOW, { uint16_t max; }) \
  X(ERR_VM_BLK_OUTPUT_TAKEN, ERR_TAG(ERR_MOD_VM, 56), ERR_SEV_LOW, { uint16_t blk_id; uint16_t obj_id; uint8_t slot; uint8_t kind; }) \
  X(ERR_VM_LOAD_EMPTY, ERR_TAG(ERR_MOD_VM, 57), ERR_SEV_LOW, { uint8_t unused; }) \
  X(ERR_VM_LOAD_ABORTED, ERR_TAG(ERR_MOD_VM, 58), ERR_SEV_MEDIUM, { uint8_t packet; }) \
  X(ERR_VM_LOAD_INCOMPLETE, ERR_TAG(ERR_MOD_VM, 59), ERR_SEV_LOW, { uint8_t kind; uint16_t id; }) \
  X(ERR_VM_NO_MEM, ERR_TAG(ERR_MOD_VM, 60), ERR_SEV_HIGH, { uint32_t size; }) \
  X(ERR_VM_BAD_ARG, ERR_TAG(ERR_MOD_VM, 61), ERR_SEV_HIGH, {  }) \
  X(ERR_VM_RANGE, ERR_TAG(ERR_MOD_VM, 62), ERR_SEV_LOW, { uint32_t val; uint32_t min; uint32_t max; }) /* a count or value outside its limits */
// clang-format on

ERR_TAGS_DECLARE(ERR_VM_MAP)

/** @brief Reports a new error at once (end of a chain nobody returns: the pass, a task, a callback). */
#define VM_RAISE(...) err_report(ERR_NEW(__VA_ARGS__))

/* Object ID constants and the names the debug output uses for the VM enums. */
#define VM_OBJ_ID_NONE     0xFFFFu
#define VM_OBJ_ID_DYN_BIT  0x8000u

static inline void vm_format_obj_id(uint16_t id, char* buf, size_t buf_size) {
  if (id == VM_OBJ_ID_NONE) {
    snprintf(buf, buf_size, "NONE");
  } else if (id & VM_OBJ_ID_DYN_BIT) {
    snprintf(buf, buf_size, "dyn#%u", (unsigned)(id & (uint16_t)~VM_OBJ_ID_DYN_BIT));
  } else {
    snprintf(buf, buf_size, "%u", (unsigned)id);
  }
}

// ===========================================================================
// String Representations for VM Core Types & Enums (Debugging & Error Logs)
// ===========================================================================

static inline const char* vm_type_name(uint8_t t) {
  switch (t) {
    case 0: return "NONE";
    case 1: return "PTR";
    case 2: return "U8";
    case 3: return "U32";
    case 4: return "I32";
    case 5: return "F";
    case 6: return "B";
    case 7: return "STR";
    default: return "UNKNOWN";
  }
}

static inline const char* vm_reg_name(uint8_t r) {
  switch (r) {
    case 0: return "object";
    case 1: return "accessor";
    case 2: return "block";
    default: return "unknown";
  }
}

static inline const char* vm_index_kind_name(uint8_t k) {
  switch (k) {
    case 0: return "LITERAL";
    case 1: return "REF";
    case 2: return "NAME";
    default: return "UNKNOWN";
  }
}

static inline const char* vm_load_state_name(uint8_t s) {
  switch (s) {
    case 0: return "EMPTY";
    case 1: return "OPEN";
    default: return "UNKNOWN";
  }
}

static inline const char* vm_run_mode_name(uint8_t m) {
  switch (m) {
    case 0: return "STOPPED";
    case 1: return "RUNNING";
    case 2: return "FROZEN";
    case 3: return "STEP";
    case 4: return "SCAN";
    case 5: return "BLOCK";
    case 6: return "BLOCK_STEP";
    default: return "UNKNOWN";
  }
}

static inline const char* vm_exec_command_name(uint8_t c) {
  switch (c) {
    case 0: return "SCAN_MODE";
    case 1: return "ONCE";
    case 2: return "BLOCK_MODE";
    case 3: return "NEXT";
    case 4: return "RESET_TO_START";
    case 5: return "NORMAL_MODE";
    case 6: return "PAUSE";
    case 7: return "RESUME";
    case 8: return "RESET";
    case 9: return "ACK_FAULT";
    default: return "UNKNOWN";
  }
}

static inline const char* vm_copy_shape_name(uint8_t r) {
  switch (r) {
    case 0: return "tree deeper than the cap, or a cycle";
    case 1: return "source slot empty, target holds an object";
    case 2: return "target slot empty, source holds an object";
    default: return "?";
  }
}

#define VM_REG_NAME(k) vm_reg_name(k)
#define VM_COPY_SHAPE_NAME(r) vm_copy_shape_name(r)
