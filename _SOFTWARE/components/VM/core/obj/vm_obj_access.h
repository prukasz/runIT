#pragma once

#include "vm_obj_access_core.h"
#include "vm_obj_access_internal.h"

/*
 * Object Access & Resolution Layer: public API
 *
 * Types and the resolution fast path live in vm_obj_access_core.h, the engines behind the
 * macros in vm_obj_access_internal.h; both are included here, so consumers include only this.
 *
 * Logic Flow:
 *   1. Target consumer macros (VM_OBJ_SCALAR_GET, VM_OBJ_SET_SCALAR, VM_PAYLOAD_GET_VAL, etc.)
 *   2. Target C APIs (read, copy, clone, link, publish, and publication)
 */

// --- Target Consumer Macros ---

/** @brief Read scalar at index `idx` (in payload) into a typed lvalue.
 * NULL/unresolved accessors, out-of-bounds index, empty payloads and non-scalar types
 * return an error and preserve output. The output expression is evaluated only after validation. */
#define VM_OBJ_SCALAR_GET_AT_IDX(output, source, idx)                             \
  ({                                                                              \
    const vm_accessor_t* __sgi_a = (source);                                      \
    vm_obj_payload_t     __p;                                                     \
    err_h                __e = vm_internal_get_scalar_slot(__sgi_a, (idx), &__p); \
    if (!__e) VM_INTERNAL_LOAD_SCALAR(&(output), (vm_obj_t_e)__p.type, __p.ptr);  \
    __e;                                                                          \
  })

/** @brief Read the first addressed scalar into a typed lvalue. */
#define VM_OBJ_SCALAR_GET(output, source) VM_OBJ_SCALAR_GET_AT_IDX((output), (source), 0)

/** @brief Checked payload read; returns err_h and preserves output on failure. */
#define VM_PAYLOAD_GET_VAL(output, payload)                                               \
  ({                                                                                      \
    vm_obj_payload_t __pv_p = (payload);                                                  \
    err_h            __pv_e = vm_internal_scalar_payload_check(__pv_p, NULL, VM_ID_NONE); \
    if (!__pv_e) VM_INTERNAL_LOAD_SCALAR(&(output), (vm_obj_t_e)__pv_p.type, __pv_p.ptr); \
    __pv_e;                                                                               \
  })

/** @brief Index relative to an accessor's resolved payload, or an owned object. */
#define VM_OBJ_SET_SCALAR_AT_IDX(source, target, index) \
  _Generic((target),                                    \
      vm_obj_h: vm_internal_set_scalar_direct,          \
      default:  vm_internal_set_scalar_slot)((target), (index), VM_VAL_OF(source), VM_TYPE_OF(source))

/** @brief Write scalar `source` into `target`, converting to stored type. */
#define VM_OBJ_SET_SCALAR(source, target) VM_OBJ_SET_SCALAR_AT_IDX((source), (target), 0)

/** @brief User-directed write: index relative to target, requiring mutable and rejecting usr_protected. */
#define VM_OBJ_SET_SCALAR_AT_IDX_USR(source, target, index) \
  _Generic((target),                                         \
      vm_obj_h: vm_internal_set_scalar_direct_usr,          \
      default:  vm_internal_set_scalar_slot_usr)((target), (index), VM_VAL_OF(source), VM_TYPE_OF(source))

/** @brief User-directed write: require mutable and reject usr_protected. */
#define VM_OBJ_SET_SCALAR_USR(source, target) VM_OBJ_SET_SCALAR_AT_IDX_USR((source), (target), 0)


// --- Target C APIs (Out-of-Line Subsystem Functions) ---

// Read
SE_MUST_USE err_h vm_obj_get_payload(vm_obj_payload_t* target, const vm_accessor_t* source);
SE_MUST_USE err_h vm_obj_get_obj(vm_obj_h* target, const vm_accessor_t* source);
/** @brief Object owning the addressed bytes; never follows a trailing PTR. */
SE_MUST_USE err_h vm_obj_get_owner(vm_obj_h* target, const vm_accessor_t* source);
/** @brief Resolve a parent object and find its child by exact byte-length tag.
 * Failure preserves *target. name need not be NUL-terminated. */
SE_MUST_USE err_h vm_obj_get_child(vm_obj_h* target, const vm_accessor_t* parent, const char* name, size_t name_len);

// Write
/** Clear without publishing freshness. Failure still returns an error. */
SE_MUST_USE err_h vm_obj_clear_quiet(vm_obj_h obj);

// Copy & Clone
/** @brief Validate the entire destination, then copy without allocating.
 * Failure preserves destination values and freshness. Requires single-writer
 * execution; overlapping source leaves follow deterministic slot order. */
SE_MUST_USE err_h vm_obj_copy_content(const vm_accessor_t* source, const vm_accessor_t* target);
SE_MUST_USE err_h vm_obj_copy_direct(vm_obj_h src, vm_obj_h dst);

/** @brief Bulk copy content from `source` into `target` (supports both accessors and raw objects). */
#define VM_OBJ_COPY_CONTENT(source, target) \
  _Generic((source),                        \
      vm_obj_h: vm_obj_copy_direct,        \
      default:  vm_obj_copy_content)((source), (target))

/** @brief Do these two trees have the same shape -- same type and element
 *  count at every level, and the same wired/unwired slots? True means
 *  the storage shapes agree; write permissions are validated separately.
 *  Tags are deliberately excluded. Clone reuse uses schema_matches instead. */
bool vm_obj_shape_matches(vm_obj_h a, vm_obj_h b);

/** @brief Storage shape plus tag identity at every node, for Clone reuse. */
bool vm_obj_schema_matches(vm_obj_h a, vm_obj_h b);

/** @brief Build a dynamic tree shaped like `src`, values left zero.
 *  Reference count zero -- owned by nothing until a pointer slot takes it, so
 *  link it in the same call or release it. Tags are carried over, since a
 *  by-name accessor onto the copy has to keep working. */
SE_MUST_USE err_h vm_obj_clone_shape(vm_obj_h* out, vm_obj_h src);

/** @brief Copy `source` into the pointer cell `target` names, building the
 *  destination first if its schema (including tags) differs. Fill completes
 *  before replacement releases the old tree; failures preserve the old slot.
 *  A matching destination is preflighted and refilled, then its holder published. */
SE_MUST_USE err_h vm_obj_clone_into(const vm_accessor_t* source, const vm_accessor_t* target);

// Link
SE_MUST_USE err_h vm_obj_link(const vm_accessor_t* child, const vm_accessor_t* target);
SE_MUST_USE err_h vm_obj_link_direct(vm_obj_h cell, uint16_t index, vm_obj_h child);

// Update / Freshness Mark
/** @brief Explicit aggregate update marking, after a successful field update.
 * Scalar writes mark their owner only; copy marks the destination subtree;
 * Clone additionally marks its holder. No implicit ancestor propagation. */
SE_MUST_USE err_h vm_obj_mark_updated(vm_obj_h obj);
