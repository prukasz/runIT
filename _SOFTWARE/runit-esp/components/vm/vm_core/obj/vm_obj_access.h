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
 *   1. Target consumer macros (VM_OBJ_SCALAR_GET, VM_OBJ_SET_SCALAR, VM_OBJ_SET_SCALAR_USR)
 *   2. Target C APIs (read, copy, clone, link, publish, and publication)
 */

// --- Target Consumer Macros ---

/** @brief Reads the scalar the accessor `source` points at (its first element) into a typed lvalue, converted. An
 * unresolved accessor, an empty or a non-scalar payload return an error and leave `output` as it was. */
#define VM_OBJ_SCALAR_GET(output, source)                                                \
  ({                                                                                     \
    vm_obj_payload_t __sg_p;                                                             \
    err_h            __sg_e = vm_acc_scalar((source), &__sg_p);                          \
    if (!__sg_e) VM_INTERNAL_LOAD_SCALAR(&(output), (vm_obj_t_e)__sg_p.type, __sg_p.ptr); \
    __sg_e;                                                                              \
  })

/** @brief A block's write of `source` to an object it produces (`obj`, an output), converted to the stored type. */
#define VM_OBJ_SET_SCALAR(source, obj) vm_internal_set_scalar((obj), VM_VAL_OF(source), VM_TYPE_OF(source))

/** @brief A user's write (SET) of `source` through the accessor `acc`: refused for an object that is not mutable or is
 * usr_protected. */
#define VM_OBJ_SET_SCALAR_USR(source, acc) vm_internal_set_scalar_usr((acc), VM_VAL_OF(source), VM_TYPE_OF(source))


// --- Target C APIs (Out-of-Line Subsystem Functions) ---

// Read
/** @brief The object an accessor names; a path ending on a PTR element names the object in that cell. */
MUST_USE err_h vm_obj_get_obj(vm_obj_h* target, const vm_accessor_t* source);

// Write
/** Clear without publishing freshness. Failure still returns an error. */
MUST_USE err_h vm_obj_clear_quiet(vm_obj_h obj);

// Copy & Clone
/** @brief Validate the entire destination, then copy without allocating.
 * Failure preserves destination values and freshness. Requires single-writer
 * execution; overlapping source leaves follow deterministic slot order. */
MUST_USE err_h vm_obj_copy_content(const vm_accessor_t* source, const vm_accessor_t* target);
MUST_USE err_h vm_obj_copy_direct(vm_obj_h src, vm_obj_h dst);

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
MUST_USE err_h vm_obj_clone_shape(vm_obj_h* out, vm_obj_h src);

/** @brief Copy `source` into the pointer cell `target` names, building the
 *  destination first if its schema (including tags) differs. Fill completes
 *  before replacement releases the old tree; failures preserve the old slot.
 *  A matching destination is preflighted and refilled, then its holder published. */
MUST_USE err_h vm_obj_clone_into(const vm_accessor_t* source, const vm_accessor_t* target);

// Link
MUST_USE err_h vm_obj_link(const vm_accessor_t* child, const vm_accessor_t* target);
MUST_USE err_h vm_obj_link_direct(vm_obj_h cell, uint16_t index, vm_obj_h child);

// Update / Freshness Mark
/** @brief Explicit aggregate update marking, after a successful field update.
 * Scalar writes mark their owner only; copy marks the destination subtree;
 * Clone additionally marks its holder. No implicit ancestor propagation. */
MUST_USE err_h vm_obj_mark_updated(vm_obj_h obj);
