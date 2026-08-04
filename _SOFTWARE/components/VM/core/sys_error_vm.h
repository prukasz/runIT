#pragma once
#include <stdint.h>
#include <stdio.h>

// Owners for the VM component (block-scripting runtime: linear allocator,
// object arena, accessor resolution, block execution, code loading).
#define SYS_VM_OWNER_MAP(X)                        \
  X(OWNER_VM_BASE, 0xA900, "OWNER_VM_BASE")         \
  X(OWNER_VM_ALLOC, 0xA901, "OWNER_VM_ALLOC")       \
  X(OWNER_VM_OBJ, 0xA902, "OWNER_VM_OBJ")           \
  X(OWNER_VM_ACCESSOR, 0xA903, "OWNER_VM_ACCESSOR") \
  X(OWNER_VM_BLOCK, 0xA904, "OWNER_VM_BLOCK")       \
  X(OWNER_VM_CODE, 0xA905, "OWNER_VM_CODE")

// `obj`/`parent_obj` carry the failing vm_obj_t* for local (serial) trace
// diagnosis -- meaningless once encoded to a remote client with no symbol
// table, so a BLE-side encoder for these should drop the field, not send it.
#define SYS_ERROR_VM_MAP(X)                                                                                          \
  X(ERR_VM_ALLOC_EXHAUSTED, struct { uint32_t requested; uint32_t remaining; })                                       \
  X(ERR_VM_ACCESSOR_UNKNOWN_ID, struct { uint16_t id; })                                                              \
  X(ERR_VM_ACCESSOR_OOB, struct { uint16_t id; uint8_t chain_pos; uint16_t index; void* obj; })                       \
  X(ERR_VM_ACCESSOR_TYPE_MISMATCH, struct { uint16_t id; uint8_t chain_pos; uint8_t expected; uint8_t actual; void* obj; }) \
  X(ERR_VM_ACCESSOR_NULL_OBJ, struct { uint16_t id; uint8_t chain_pos; void* parent_obj; })                           \
  X(ERR_VM_ACCESSOR_DEPTH_EXCEEDED, struct { uint16_t id; })                                                          \
  X(ERR_VM_ACCESSOR_INDEX_FAILED, struct { uint16_t id; uint8_t chain_pos; })                                         \
  X(ERR_VM_ACCESSOR_NOT_MUTABLE, struct { uint16_t id; uint8_t chain_pos; void* obj; })                               \
  X(ERR_VM_BLOCK_INPUT_UNRESOLVED, struct { uint16_t block_idx; uint8_t input_idx; })

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
  X(ERR_VM_ACCESSOR_NOT_MUTABLE)   \
  X(ERR_VM_BLOCK_INPUT_UNRESOLVED)

#define LOG_BODY_ERR_VM_ALLOC_EXHAUSTED(p, out, out_size) \
  snprintf((out), (out_size), "vm arena exhausted: requested %lu, %lu remaining", (unsigned long)(p)->requested, (unsigned long)(p)->remaining)
#define LOG_BODY_ERR_VM_ACCESSOR_UNKNOWN_ID(p, out, out_size) \
  snprintf((out), (out_size), "accessor referenced unknown object id %u", (p)->id)
#define LOG_BODY_ERR_VM_ACCESSOR_OOB(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: index %u out of range at chain position %u (obj=%p)", (p)->id, (p)->index, (p)->chain_pos, (p)->obj)
#define LOG_BODY_ERR_VM_ACCESSOR_TYPE_MISMATCH(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: expected type %u at chain position %u, got %u (obj=%p)", (p)->id, (p)->expected, (p)->chain_pos, (p)->actual, (p)->obj)
#define LOG_BODY_ERR_VM_ACCESSOR_NULL_OBJ(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: chain position %u dereferenced a null object (parent=%p)", (p)->id, (p)->chain_pos, (p)->parent_obj)
#define LOG_BODY_ERR_VM_ACCESSOR_DEPTH_EXCEEDED(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u exceeded max nesting depth", (p)->id)
#define LOG_BODY_ERR_VM_ACCESSOR_INDEX_FAILED(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: dynamic index at chain position %u failed to resolve", (p)->id, (p)->chain_pos)
#define LOG_BODY_ERR_VM_ACCESSOR_NOT_MUTABLE(p, out, out_size) \
  snprintf((out), (out_size), "accessor %u: write rejected at chain position %u, target is not mutable (obj=%p)", (p)->id, (p)->chain_pos, (p)->obj)
#define LOG_BODY_ERR_VM_BLOCK_INPUT_UNRESOLVED(p, out, out_size) \
  snprintf((out), (out_size), "block %u input %u failed to resolve", (p)->block_idx, (p)->input_idx)

/**
 * @brief SE_EMIT_ERR() relies on an ambient `#define OWNER` per source file
 * (see the [[runit]] skill's module owner tagging convention) - that doesn't
 * fit vm_alloc.h/vm_obj_access.h, which are header-only and get pulled into
 * many different .c files, each with its own OWNER. This variant takes the
 * owner explicitly so it behaves the same regardless of include order or
 * whatever OWNER the including file has defined.
 */
#define SE_EMIT_ERR_OWNED(owner, tag_name, ...)                                                \
  do {                                                                                          \
    err_h __e = SE_alloc_bytes(sizeof(err_payload_##tag_name##_t), tag_name, (owner));          \
    *((err_payload_##tag_name##_t*)__e->payload) = (err_payload_##tag_name##_t){__VA_ARGS__};   \
    SE_push_to_handler(__e);                                                                    \
  } while (0)

// Same reasoning, for the return-style macros (SE_ERR_NEW / SE_RET_ERR /
// SE_CHECK_NOT_NULL / SE_CHECK_IF_ALLOCATED) - these build and *return* an
// err_h rather than pushing it, but still rely on the ambient OWNER token.
#define SE_ERR_NEW_OWNED(owner, tag_name, ...)                                                \
  ({                                                                                           \
    err_h __e = SE_alloc_bytes(sizeof(err_payload_##tag_name##_t), tag_name, (owner));         \
    *((err_payload_##tag_name##_t*)__e->payload) = (err_payload_##tag_name##_t){__VA_ARGS__};  \
    __e;                                                                                       \
  })

#define SE_RET_ERR_OWNED(owner, tag_name, ...) return SE_ERR_NEW_OWNED((owner), tag_name, __VA_ARGS__)

// Same reasoning again, for SE_WRAP_ERR - chains rc_err as the new node's
// next_cause instead of leaving it NULL, the same linked-cause shape
// SE_WRAP_DEV_ERR uses for device errors, just with an explicit owner.
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
