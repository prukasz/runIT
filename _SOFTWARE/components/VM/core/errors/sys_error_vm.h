#pragma once
#include <stdint.h>
#include <stdio.h>

// Owners for the VM component (block-scripting runtime: program storage,
// object arena, accessor resolution, block execution, code loading).
#define SYS_VM_OWNER_MAP(X)                        \
  X(OWNER_VM_BASE, 0xA900, "OWNER_VM_BASE")         \
  X(OWNER_VM_STORE, 0xA901, "OWNER_VM_STORE")       \
  X(OWNER_VM_OBJ, 0xA902, "OWNER_VM_OBJ")           \
  X(OWNER_VM_ACCESSOR, 0xA903, "OWNER_VM_ACCESSOR") \
  X(OWNER_VM_BLOCK, 0xA904, "OWNER_VM_BLOCK")       \
  X(OWNER_VM_CODE, 0xA905, "OWNER_VM_CODE")         \
  X(OWNER_VM_LOADER, 0xA906, "OWNER_VM_LOADER")     \
  X(OWNER_DEC_VM_LOADER, 0xA907, "OWNER_DEC_VM_LOADER") \
  X(OWNER_VM_EXEC, 0xA908, "OWNER_VM_EXEC")

// `obj_id`/`parent_id` carry the failing object ID (or VM_OBJ_ID_DYN_BIT | dyn_id, or VM_OBJ_ID_NONE)
// allowing direct lookup via vm_obj_lookup_by_id().
#define SYS_ERROR_VM_MAP(X)                                                                                          \
  X(ERR_VM_ALLOC_EXHAUSTED, struct { uint32_t requested; uint32_t remaining; })                                       \
  X(ERR_VM_ACCESSOR_UNKNOWN_ID, struct { uint16_t id; })                                                              \
  X(ERR_VM_ACCESSOR_OOB, struct { uint16_t id; uint8_t chain_pos; uint16_t index; uint16_t obj_id; })                  \
  X(ERR_VM_ACCESSOR_TYPE_MISMATCH, struct { uint16_t id; uint8_t chain_pos; uint8_t expected; uint8_t actual; uint16_t obj_id; }) \
  X(ERR_VM_ACCESSOR_NULL_OBJ, struct { uint16_t id; uint8_t chain_pos; uint16_t parent_id; })                           \
  X(ERR_VM_ACCESSOR_DEPTH_EXCEEDED, struct { uint16_t id; })                                                          \
  X(ERR_VM_ACCESSOR_INDEX_FAILED, struct { uint16_t id; uint8_t chain_pos; })                                         \
  X(ERR_VM_ACCESSOR_NAME_NOT_FOUND, struct { uint16_t id; uint8_t chain_pos; char name[16]; })                        \
  X(ERR_VM_ACCESSOR_NOT_MUTABLE, struct { uint16_t id; uint8_t chain_pos; uint16_t obj_id; })                          \
  X(ERR_VM_BLOCK_INPUT_UNRESOLVED, struct { uint16_t block_idx; uint8_t input_idx; })                                 \
  X(ERR_VM_BLOCK_PIN_MISSING, struct { uint16_t block_idx; uint8_t pin_id; uint8_t is_out; })                         \
  X(ERR_VM_BLOCK_PIN_UNLINKED, struct { uint16_t block_idx; uint8_t pin_id; uint8_t is_out; })                        \
  X(ERR_VM_BLOCK_FAILED, struct { uint16_t block_idx; uint8_t block_type; })                                          \
  X(ERR_VM_OBJ_COPY_MISMATCH, struct { uint8_t src_type; uint8_t dst_type; uint16_t src_size; uint16_t dst_size; })   \
  X(ERR_VM_OBJ_COPY_SHAPE, struct { uint16_t index; uint8_t depth; uint8_t reason; })                                 \
  X(ERR_VM_OBJ_NOT_MUTABLE, struct { uint16_t obj_id; })                                                               \
  X(ERR_VM_OBJ_OOB, struct { uint16_t index; uint16_t obj_id; })                                                       \
  X(ERR_VM_OBJ_NOT_PTR, struct { uint8_t actual; uint16_t obj_id; })                                                   \
  X(ERR_VM_OBJ_BAD_TYPE, struct { uint8_t type; })                                                                    \
  X(ERR_VM_OBJ_EMPTY, struct { uint8_t type; })                                                                       \
  X(ERR_VM_OBJ_BAD_SIZE, struct { uint8_t type; uint16_t payload_size; uint8_t width; })                              \
  X(ERR_VM_OBJ_NAME_TOO_LONG, struct { uint8_t len; })                                                                \
  X(ERR_VM_OBJ_RETENTIVE_PTR, struct { uint8_t type; })                                                               \
  X(ERR_VM_LOAD_BAD_STATE, struct { uint8_t state; uint8_t expected; })                                               \
  X(ERR_VM_LOAD_TOO_BIG, struct { uint32_t requested; uint32_t available; })                                          \
  X(ERR_VM_LOAD_DATA_RANGE, struct { uint16_t id; uint16_t start_idx; uint16_t len; uint16_t items; })                \
  X(ERR_VM_LOAD_SHORT_RECORD, struct { uint8_t packet; uint16_t need; uint16_t got; })                                \
  X(ERR_VM_ACC_INDEX_OOB, struct { uint16_t acc_id; uint8_t pos; uint8_t count; })                                    \
  X(ERR_VM_ACC_BAD_KIND, struct { uint16_t acc_id; uint8_t pos; uint8_t kind; })                                      \
  X(ERR_VM_REG_OOB, struct { uint8_t kind; uint16_t id; uint16_t count; })                                            \
  X(ERR_VM_REG_DUP, struct { uint8_t kind; uint16_t id; })                                                            \
  X(ERR_VM_BLK_BAD_SHAPE, struct { uint16_t blk_id; uint8_t in_cnt; uint8_t q_cnt; })                                 \
  X(ERR_VM_BLK_BAD_REF, struct { uint16_t blk_id; uint16_t ref_id; uint8_t slot; uint8_t kind; })                      \
  X(ERR_VM_DYN_FULL, struct { uint16_t limit; })                                                                      \
  X(ERR_VM_BLK_UNKNOWN_TYPE, struct { uint16_t blk_id; uint8_t block_type; })                                         \
  X(ERR_VM_EXEC_BLOCK_HUNG, struct { uint16_t block_idx; uint16_t ms; })                                              \
  X(ERR_VM_EXEC_SPAN_DEPTH, struct { uint16_t block_idx; uint8_t depth; })                                            \
  X(ERR_VM_EXEC_BAD_SPAN, struct { uint16_t block_idx; uint16_t start; uint16_t end; })                               \
  X(ERR_VM_EVENT_OVERFLOW, struct { uint16_t type; uint16_t depth; uint16_t dropped; })                               \
  X(ERR_VM_EXPR_BAD_CODE, struct { uint16_t block_idx; uint16_t pc; uint8_t opcode; uint8_t reason; })                \
  X(ERR_VM_EXPR_MATH, struct { uint16_t block_idx; uint16_t pc; uint8_t opcode; uint8_t reason; })                    \
  X(ERR_VM_FOR_BAD_LOOP, struct { uint16_t block_idx; uint32_t turns; uint16_t cap; uint8_t reason; }) \
  X(ERR_VM_OBJ_OWNERSHIP, struct { uint8_t reason; uint8_t limit; }) \
  X(ERR_VM_OBJ_USR_PROTECTED, struct { uint16_t obj_id; }) \
  X(ERR_VM_LOAD_RUNNING, struct { uint8_t mode; }) \
  X(ERR_VM_EXEC_CONTROL, struct { uint8_t command; uint8_t mode; }) \
  X(ERR_VM_OVERRIDE_PTR_UNSUPPORTED, struct { uint16_t obj_id; }) \
  X(ERR_VM_OVERRIDE_BAD_RECORD, struct { uint16_t obj_id; uint16_t declared_len; uint16_t item_size; }) \
  X(ERR_VM_EXEC_SELF_BARRIER, struct { uint8_t operation; }) \
  X(ERR_VM_EXEC_FAULT_LATCHED, struct { uint8_t device_id; uint16_t root_tag; uint32_t root_owner; })

/**
 * @brief Human-readable descriptions for the VM tags - see
 * SE_describe_payload() in sys_error.h. Same two-step split as every other
 * module: this file only supplies LOG_BODY_<tag> text macros, sys_error.h
 * stamps them into typed logger functions once err_payload_<tag>_t exists.
 */
#define SYS_ERROR_VM_LOGGER_MAP(X) \
  X(ERR_VM_ALLOC_EXHAUSTED)        \
  X(ERR_VM_ACCESSOR_UNKNOWN_ID)    \
  X(ERR_VM_ACCESSOR_OOB)           \
  X(ERR_VM_ACCESSOR_TYPE_MISMATCH) \
  X(ERR_VM_ACCESSOR_NULL_OBJ)      \
  X(ERR_VM_ACCESSOR_DEPTH_EXCEEDED) \
  X(ERR_VM_ACCESSOR_INDEX_FAILED)  \
  X(ERR_VM_ACCESSOR_NAME_NOT_FOUND) \
  X(ERR_VM_ACCESSOR_NOT_MUTABLE)   \
  X(ERR_VM_BLOCK_INPUT_UNRESOLVED) \
  X(ERR_VM_BLOCK_PIN_MISSING)      \
  X(ERR_VM_BLOCK_PIN_UNLINKED)     \
  X(ERR_VM_BLOCK_FAILED)           \
  X(ERR_VM_OBJ_COPY_MISMATCH)      \
  X(ERR_VM_OBJ_COPY_SHAPE)         \
  X(ERR_VM_OBJ_NOT_MUTABLE)        \
  X(ERR_VM_OBJ_OOB)                \
  X(ERR_VM_OBJ_NOT_PTR)            \
  X(ERR_VM_OBJ_BAD_TYPE)           \
  X(ERR_VM_OBJ_EMPTY)              \
  X(ERR_VM_OBJ_BAD_SIZE)           \
  X(ERR_VM_OBJ_NAME_TOO_LONG)      \
  X(ERR_VM_OBJ_RETENTIVE_PTR)      \
  X(ERR_VM_LOAD_BAD_STATE)         \
  X(ERR_VM_LOAD_TOO_BIG)           \
  X(ERR_VM_LOAD_DATA_RANGE)        \
  X(ERR_VM_LOAD_SHORT_RECORD)      \
  X(ERR_VM_ACC_INDEX_OOB)          \
  X(ERR_VM_ACC_BAD_KIND)           \
  X(ERR_VM_REG_OOB)                \
  X(ERR_VM_REG_DUP)                \
  X(ERR_VM_BLK_BAD_SHAPE)          \
  X(ERR_VM_BLK_BAD_REF)            \
  X(ERR_VM_DYN_FULL)               \
  X(ERR_VM_BLK_UNKNOWN_TYPE)       \
  X(ERR_VM_EXEC_BLOCK_HUNG)        \
  X(ERR_VM_EXEC_SPAN_DEPTH)        \
  X(ERR_VM_EXEC_BAD_SPAN)          \
  X(ERR_VM_EVENT_OVERFLOW)         \
  X(ERR_VM_EXPR_BAD_CODE)          \
  X(ERR_VM_EXPR_MATH)              \
  X(ERR_VM_FOR_BAD_LOOP) \
  X(ERR_VM_OBJ_OWNERSHIP) \
  X(ERR_VM_OBJ_USR_PROTECTED) \
  X(ERR_VM_LOAD_RUNNING) \
  X(ERR_VM_EXEC_CONTROL) \
  X(ERR_VM_OVERRIDE_PTR_UNSUPPORTED) \
  X(ERR_VM_OVERRIDE_BAD_RECORD) \
  X(ERR_VM_EXEC_SELF_BARRIER) \
  X(ERR_VM_EXEC_FAULT_LATCHED)

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

#define LOG_BODY_ERR_VM_OBJ_OWNERSHIP(p, out, out_size) \
  snprintf((out), (out_size), "dynamic ownership: %s (depth limit %u)", (p)->reason == 0 ? "cycle" : "too deep", (p)->limit)
#define LOG_BODY_ERR_VM_OBJ_USR_PROTECTED(p, out, out_size) do { \
    char _id[16]; \
    vm_format_obj_id((p)->obj_id, _id, sizeof(_id)); \
    snprintf((out), (out_size), "object is protected from user writes (obj_id=%s)", _id); \
  } while (0)

#define LOG_BODY_ERR_VM_ALLOC_EXHAUSTED(p, out, out_size) \
  snprintf((out), (out_size), "vm arena exhausted: requested %lu, %lu remaining", (unsigned long)(p)->requested, (unsigned long)(p)->remaining)
#define LOG_BODY_ERR_VM_ACCESSOR_UNKNOWN_ID(p, out, out_size) \
  snprintf((out), (out_size), "accessor referenced unknown object id %u", (p)->id)
#define LOG_BODY_ERR_VM_ACCESSOR_OOB(p, out, out_size) do { \
    char _id[16]; \
    vm_format_obj_id((p)->obj_id, _id, sizeof(_id)); \
    snprintf((out), (out_size), "accessor %u: index %u out of range at chain position %u (obj_id=%s)", (p)->id, (p)->index, (p)->chain_pos, _id); \
  } while (0)
#define LOG_BODY_ERR_VM_ACCESSOR_TYPE_MISMATCH(p, out, out_size) do { \
    char _id[16]; \
    vm_format_obj_id((p)->obj_id, _id, sizeof(_id)); \
    snprintf((out), (out_size), "accessor %u: expected type %u (%s) at chain position %u, got %u (%s) (obj_id=%s)", \
             (p)->id, (p)->expected, vm_type_name((p)->expected), (p)->chain_pos, (p)->actual, vm_type_name((p)->actual), _id); \
  } while (0)
#define LOG_BODY_ERR_VM_ACCESSOR_NULL_OBJ(p, out, out_size) do { \
    char _id[16]; \
    vm_format_obj_id((p)->parent_id, _id, sizeof(_id)); \
    snprintf((out), (out_size), "accessor %u: chain position %u dereferenced a null object (parent_id=%s)", (p)->id, (p)->chain_pos, _id); \
  } while (0)
#define LOG_BODY_ERR_VM_ACCESSOR_DEPTH_EXCEEDED(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u exceeded max nesting depth", (p)->id)
#define LOG_BODY_ERR_VM_ACCESSOR_INDEX_FAILED(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: dynamic index at chain position %u failed to resolve", (p)->id, (p)->chain_pos)
#define LOG_BODY_ERR_VM_ACCESSOR_NAME_NOT_FOUND(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: no child tagged '%s' at chain position %u", (p)->id, (p)->name, (p)->chain_pos)
#define LOG_BODY_ERR_VM_ACCESSOR_NOT_MUTABLE(p, out, out_size) do { \
    char _id[16]; \
    vm_format_obj_id((p)->obj_id, _id, sizeof(_id)); \
    snprintf((out), (out_size), "accessor %u: write rejected at chain position %u, target is not mutable (obj_id=%s)", (p)->id, (p)->chain_pos, _id); \
  } while (0)
#define LOG_BODY_ERR_VM_BLOCK_INPUT_UNRESOLVED(p, out, out_size) \
  snprintf((out), (out_size), "block %u input %u failed to resolve", (p)->block_idx, (p)->input_idx)
#define LOG_BODY_ERR_VM_BLOCK_PIN_MISSING(p, out, out_size) \
  snprintf((out), (out_size), "block %u has no %s pin %u", (p)->block_idx, (p)->is_out ? "output" : "input", (p)->pin_id)
#define LOG_BODY_ERR_VM_BLOCK_PIN_UNLINKED(p, out, out_size) \
  snprintf((out), (out_size), "block %u %s pin %u is not linked", (p)->block_idx, (p)->is_out ? "output" : "input", (p)->pin_id)
#define LOG_BODY_ERR_VM_BLOCK_FAILED(p, out, out_size) \
  snprintf((out), (out_size), "block %u (type %u) failed", (p)->block_idx, (p)->block_type)
#define LOG_BODY_ERR_VM_OBJ_COPY_MISMATCH(p, out, out_size)                                                  \
  snprintf((out), (out_size), "object copy mismatch: src type %u (%s) size %u, dst type %u (%s) size %u", (p)->src_type, \
           vm_type_name((p)->src_type), (p)->src_size, (p)->dst_type, vm_type_name((p)->dst_type), (p)->dst_size)
/* The two trees disagreed about their *structure* rather than about a value:
   COPY_MISMATCH already says "these two payloads do not match", so this one
   only has to say where the walk gave up. `reason` is a VM_COPY_SHAPE_* code
   from vm_obj_access.h. */
#define LOG_BODY_ERR_VM_OBJ_COPY_SHAPE(p, out, out_size)                                                    \
  snprintf((out), (out_size), "object copy: %s at depth %u, slot %u", vm_copy_shape_name((p)->reason),      \
           (p)->depth, (p)->index)
/* The object-level halves of three accessor errors. An accessor id and a chain
   position are only meaningful when a chain was walked, and the direct entry
   points -- vm_internal_set_scalar_direct(), vm_obj_link_direct(), and the walk
   inside vm_obj_copy_content() once it is below where the chain ended -- were
   handed a raw handle instead. They used to fill both fields with 0, which is
   not a sentinel: VM_ID_NONE is 0xFFFF, so every one of those traces named
   accessor 0, an innocent bystander. */
#define LOG_BODY_ERR_VM_OBJ_NOT_MUTABLE(p, out, out_size) do { \
    char _id[16]; \
    vm_format_obj_id((p)->obj_id, _id, sizeof(_id)); \
    snprintf((out), (out_size), "object is not mutable (obj_id=%s)", _id); \
  } while (0)
#define LOG_BODY_ERR_VM_OBJ_OOB(p, out, out_size) do { \
    char _id[16]; \
    vm_format_obj_id((p)->obj_id, _id, sizeof(_id)); \
    snprintf((out), (out_size), "object index %u is past the end of its payload (obj_id=%s)", (p)->index, _id); \
  } while (0)
#define LOG_BODY_ERR_VM_OBJ_NOT_PTR(p, out, out_size) do { \
    char _id[16]; \
    vm_format_obj_id((p)->obj_id, _id, sizeof(_id)); \
    snprintf((out), (out_size), "object holds type %u (%s); a link needs VM_OBJ_PTR (obj_id=%s)", (p)->actual, vm_type_name((p)->actual), _id); \
  } while (0)
#define LOG_BODY_ERR_VM_OBJ_BAD_TYPE(p, out, out_size) \
  snprintf((out), (out_size), "object create: unknown type %u (%s)", (p)->type, vm_type_name((p)->type))
#define LOG_BODY_ERR_VM_OBJ_EMPTY(p, out, out_size) \
  snprintf((out), (out_size), "object create: type %u (%s) with zero items has no storage", (p)->type, vm_type_name((p)->type))
#define LOG_BODY_ERR_VM_OBJ_BAD_SIZE(p, out, out_size)                                                       \
  snprintf((out), (out_size), "object create: payload of %u bytes is not a whole number of %u-byte type %u (%s)", \
           (p)->payload_size, (p)->width, (p)->type, vm_type_name((p)->type))
#define LOG_BODY_ERR_VM_OBJ_NAME_TOO_LONG(p, out, out_size) \
  snprintf((out), (out_size), "object create: name is %u chars, max is 15", (p)->len)
#define LOG_BODY_ERR_VM_OBJ_RETENTIVE_PTR(p, out, out_size) \
  snprintf((out), (out_size), "object create: retentive is meaningless for pointer type %u (%s)", (p)->type, vm_type_name((p)->type))
#define LOG_BODY_ERR_VM_DYN_FULL(p, out, out_size) \
  snprintf((out), (out_size), "dynamic object register is full: %u live, none released", (p)->limit)
#define LOG_BODY_ERR_VM_LOAD_BAD_STATE(p, out, out_size) \
  snprintf((out), (out_size), "loader in state %u (%s), this packet needs state %u (%s)", (p)->state, vm_load_state_name((p)->state), (p)->expected, vm_load_state_name((p)->expected))
#define LOG_BODY_ERR_VM_LOAD_TOO_BIG(p, out, out_size)                                            \
  snprintf((out), (out_size), "program needs %lu bytes, VM pool holds %lu", (unsigned long)(p)->requested, \
           (unsigned long)(p)->available)
#define LOG_BODY_ERR_VM_LOAD_DATA_RANGE(p, out, out_size)                                             \
  snprintf((out), (out_size), "object %u: write of %u elements at %u exceeds its %u items", (p)->id, (p)->len, \
           (p)->start_idx, (p)->items)
#define LOG_BODY_ERR_VM_LOAD_SHORT_RECORD(p, out, out_size) \
  snprintf((out), (out_size), "packet 0x%02X truncated: need %u bytes, got %u", (p)->packet, (p)->need, (p)->got)
#define LOG_BODY_ERR_VM_ACC_INDEX_OOB(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: index position %u past its %u declared indices", (p)->acc_id, (p)->pos, (p)->count)
#define LOG_BODY_ERR_VM_ACC_BAD_KIND(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u index %u: unknown kind %u (%s)", (p)->acc_id, (p)->pos, (p)->kind, vm_index_kind_name((p)->kind))
#define LOG_BODY_ERR_VM_REG_OOB(p, out, out_size)   snprintf((out), (out_size), "%s registry: id %u is outside the %u-entry table", vm_reg_name((p)->kind), (p)->id, (p)->count)
#define LOG_BODY_ERR_VM_REG_DUP(p, out, out_size)   snprintf((out), (out_size), "%s registry: id %u is already bound", vm_reg_name((p)->kind), (p)->id)
#define LOG_BODY_ERR_VM_BLK_BAD_SHAPE(p, out, out_size) \
  snprintf((out), (out_size), "block %u: %u inputs / %u outputs is not a buildable shape", (p)->blk_id, (p)->in_cnt, (p)->q_cnt)
/* slot is the pin index; kind says which table the id failed to resolve in,
   so one tag covers all four wiring points instead of four near-identical ones */
#define LOG_BODY_ERR_VM_BLK_BAD_REF(p, out, out_size)                                                                                                                        \
  snprintf((out), (out_size), "block %u %s%u: id %u does not resolve", (p)->blk_id,                                                                                          \
           (p)->kind == 0   ? "input "                                                                                                                                       \
           : (p)->kind == 1 ? "output "                                                                                                                                      \
           : (p)->kind == 2 ? "EN"                                                                                                                                           \
                            : "ENO",                                                                                                                                         \
           (p)->slot, (p)->ref_id)

#define LOG_BODY_ERR_VM_BLK_UNKNOWN_TYPE(p, out, out_size) \
  snprintf((out), (out_size), "block %u: nothing in the palette runs type %u", (p)->blk_id, (p)->block_type)
#define LOG_BODY_ERR_VM_EXEC_BLOCK_HUNG(p, out, out_size) \
  snprintf((out), (out_size), "block %u has been executing for over %u ms", (p)->block_idx, (p)->ms)
#define LOG_BODY_ERR_VM_EXEC_SPAN_DEPTH(p, out, out_size) \
  snprintf((out), (out_size), "block %u: span nesting exceeded depth %u", (p)->block_idx, (p)->depth)
#define LOG_BODY_ERR_VM_EXEC_BAD_SPAN(p, out, out_size) \
  snprintf((out), (out_size), "block %u: span [%u,%u) is not a valid forward range inside its enclosing range", (p)->block_idx, (p)->start, (p)->end)
#define LOG_BODY_ERR_VM_EVENT_OVERFLOW(p, out, out_size)                                                     \
  snprintf((out), (out_size), "vm event buffer full at %u per cycle: %u dropped, latest of callback type %u", \
           (p)->depth, (p)->dropped, (p)->type)
/* Both expression tags name the byte offset of the *instruction*, not the
   cursor past its operand, so a trace points at what a disassembly shows.
   `reason` is a VM_EXPR_BAD_* / VM_EXPR_MATH_* code from vm_block_expr.h. */
#define VM_EXPR_BAD_NAME(r)                                                                                 \
  ((r) == 0 ? "header does not fit custom_data" : (r) == 1 ? "block has no output"                          \
   : (r) == 2 ? "unknown opcode" : (r) == 3 ? "bad operand"                                                 \
   : (r) == 4 ? "stack underflow" : (r) == 5 ? "stack overflow"                                             \
   : (r) == 6 ? "stack did not end with one value" : "?")
#define VM_EXPR_MATH_NAME(r) \
  ((r) == 0 ? "divide by zero" : (r) == 1 ? "operand outside domain" : (r) == 2 ? "result not finite" : "?")
#define LOG_BODY_ERR_VM_EXPR_BAD_CODE(p, out, out_size)                                                     \
  snprintf((out), (out_size), "block %u expression at pc %u (op %u): %s", (p)->block_idx, (p)->pc,          \
           (p)->opcode, VM_EXPR_BAD_NAME((p)->reason))
#define LOG_BODY_ERR_VM_EXPR_MATH(p, out, out_size)                                                         \
  snprintf((out), (out_size), "block %u expression at pc %u (op %u): %s", (p)->block_idx, (p)->pc,          \
           (p)->opcode, VM_EXPR_MATH_NAME((p)->reason))
// Loop turn budget exceeded or iterator overflowed (VM_FOR_BAD_* reason code)
#define VM_FOR_BAD_NAME(r) ((r) == 0 ? "ran its whole turn budget" : (r) == 1 ? "iterator went non-finite" : "?")
#define LOG_BODY_ERR_VM_FOR_BAD_LOOP(p, out, out_size)                                                      \
  snprintf((out), (out_size), "block %u loop %s after %lu of %u turns", (p)->block_idx,                     \
           VM_FOR_BAD_NAME((p)->reason), (unsigned long)(p)->turns, (p)->cap)
#define LOG_BODY_ERR_VM_EXEC_CONTROL(p, out, out_size) \
  snprintf((out), (out_size), "execution command %u (%s) unavailable in mode %u (%s) (or program locked)", \
           (p)->command, vm_exec_command_name((p)->command), (p)->mode, vm_run_mode_name((p)->mode))
#define LOG_BODY_ERR_VM_LOAD_RUNNING(p, out, out_size) \
  snprintf((out), (out_size), "program initialization requires stopped execution (mode %u: %s)", \
           (p)->mode, vm_run_mode_name((p)->mode))
#define LOG_BODY_ERR_VM_OVERRIDE_PTR_UNSUPPORTED(p, out, out_size) do { \
    char _id[16]; \
    vm_format_obj_id((p)->obj_id, _id, sizeof(_id)); \
    snprintf((out), (out_size), "runtime override cannot write pointer object (obj_id=%s)", _id); \
  } while (0)
#define LOG_BODY_ERR_VM_OVERRIDE_BAD_RECORD(p, out, out_size) do { \
    char _id[16]; \
    vm_format_obj_id((p)->obj_id, _id, sizeof(_id)); \
    snprintf((out), (out_size), "runtime override record malformed (obj_id=%s, declared=%u, item=%u bytes)", \
             _id, (p)->declared_len, (p)->item_size); \
  } while (0)
#define LOG_BODY_ERR_VM_EXEC_SELF_BARRIER(p, out, out_size) \
  snprintf((out), (out_size), "%s", \
           (p)->operation == 0 \
               ? "active VM pass cannot wait for its own stop; cancellation was requested without waiting" \
               : "active VM pass cannot acquire its own program-mutation barrier")
#define LOG_BODY_ERR_VM_EXEC_FAULT_LATCHED(p, out, out_size) \
  snprintf((out), (out_size), "VM restart blocked by device %u fault (root owner=0x%04lX, tag=%u)", \
           (p)->device_id, (unsigned long)(p)->root_owner, (unsigned)(p)->root_tag)


/** @brief SE_EMIT_ERR variant with explicit owner for shared headers. */
#define SE_EMIT_ERR_OWNED(owner, tag_name, ...)                                                \
  do {                                                                                          \
    err_h __e = SE_alloc_bytes(sizeof(err_payload_##tag_name##_t), tag_name, (owner));          \
    *((err_payload_##tag_name##_t*)__e->payload) = (err_payload_##tag_name##_t){__VA_ARGS__};   \
    SE_push_to_handler(__e);                                                                    \
  } while (0)

// Return-style error macro with explicit owner
#define SE_ERR_NEW_OWNED(owner, tag_name, ...)                                                \
  ({                                                                                           \
    err_h __e = SE_alloc_bytes(sizeof(err_payload_##tag_name##_t), tag_name, (owner));         \
    *((err_payload_##tag_name##_t*)__e->payload) = (err_payload_##tag_name##_t){__VA_ARGS__};  \
    __e;                                                                                       \
  })

#define SE_RET_ERR_OWNED(owner, tag_name, ...) return SE_ERR_NEW_OWNED((owner), tag_name, __VA_ARGS__)

// Wrap existing error with next_cause and explicit owner
#define SE_WRAP_ERR_OWNED(owner, rc_err, tag_name, ...)                                      \
  ({                                                                                          \
    err_h __new_err = SE_ERR_NEW_OWNED((owner), tag_name, __VA_ARGS__);                       \
    __new_err->next_cause = (rc_err);                                                         \
    __new_err;                                                                                \
  })

#define SE_CHECK_NOT_NULL_OWNED(owner, ptr)              \
  do {                                                   \
    if ((ptr) == NULL) {                                 \
      SE_RET_ERR_OWNED((owner), ERR_NULL_PTR, 0);        \
    }                                                    \
  } while (0)

#define SE_CHECK_IF_ALLOCATED_OWNED(owner, ptr)          \
  do {                                                   \
    if ((ptr) == NULL) {                                 \
      SE_RET_ERR_OWNED((owner), ERR_BASE_NO_MEM, 0);     \
    }                                                    \
  } while (0)
