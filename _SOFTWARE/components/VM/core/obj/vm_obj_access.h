#pragma once

#include <limits.h>
#include <math.h>
#include "vm_errors.h"
#include "vm_obj.h"
#include "vm_obj_dyn.h"
#include "vm_store.h"

/*
 * Object Access & Resolution Layer
 *
 * Logic Flow:
 *   1. Types & Core Data Structures (vm_obj_payload_t, vm_val_t, vm_accessor_t)
 *   2. Helpers (Object/Store lookups, resolution, read/write conversions, and inline store)
 *   3. Target APIs:
 *      - Direct inline mutations (vm_internal_set_scalar_direct)
 *      - Target consumer macros (VM_OBJ_SCALAR_GET, VM_OBJ_SET_SCALAR, VM_PAYLOAD_GET_VAL, etc.)
 *      - Target C APIs (read, copy, clone, link, publish, and publication)
 */

// ===========================================================================
// 1. Types & Core Data Structures
// ===========================================================================

/** @brief vm_accessor_t.flags bit: a resolved payload is cached in c_payload. */
#define VM_ACC_F_CACHED 0x01u

/* `reason` in ERR_VM_OBJ_COPY_SHAPE -- kept in step with VM_COPY_SHAPE_NAME() in sys_error_vm.h */
#define VM_COPY_SHAPE_DEPTH     0u  // ran out of depth, or the tree loops
#define VM_COPY_SHAPE_SRC_EMPTY 1u  // source slot unwired, target holds an object
#define VM_COPY_SHAPE_DST_EMPTY 2u  // target slot unwired, source holds an object

/** @brief Where a value lives: address in arena, owning object, element count, type. (12 bytes). */
typedef struct vm_obj_payload_t {
  void*    ptr;    // NULL when unresolved
  vm_obj_h owner;  // object the payload's bytes belong to
  uint16_t count;  // number of `type` elements available at ptr
  uint8_t  type;   // vm_obj_t_e
  uint8_t  _pad;
} vm_obj_payload_t;

_Static_assert(sizeof(vm_obj_payload_t) == 12, "vm_obj_payload_t must be 12 bytes");

/** @brief Scratch value wide enough for any scalar vm_obj_t_e. */
typedef union {
  uint8_t  u8;
  uint32_t u32;
  int32_t  i32;
  float    f;
} vm_val_t;

typedef struct vm_accessor_t vm_accessor_t;

typedef enum vm_index_kind_e {
  VM_IDX_LITERAL = 0,  // fixed position, known at compile time
  VM_IDX_REF     = 1,  // resolve another accessor and read it as the index
  VM_IDX_NAME    = 2,  // match a child object's tag
} vm_index_kind_e;

/** @brief String representation of accessor index kind for debugging. */
static inline const char* vm_index_kind_str(vm_index_kind_e k) {
  return vm_index_kind_name((uint8_t)k);
}

typedef struct {
  uint8_t kind;      // vm_index_kind_e
  uint8_t name_len;  // VM_IDX_NAME only: strlen(name), measured once at build
  union {
    uint32_t             value;  // VM_IDX_LITERAL
    const vm_accessor_t* ref;    // VM_IDX_REF
    const char*          name;   // VM_IDX_NAME
  };
} vm_index_t;

#define VM_IDX_BY_NAME(str) {.kind = VM_IDX_NAME, .name_len = (uint8_t)(sizeof(str) - 1), .name = (str)}

struct vm_accessor_t {
  uint16_t          id;         // root object's id in registry
  uint8_t           count;      // number of chained indices; 0 = whole object
  uint8_t           flags;      // VM_ACC_F_*
  const vm_index_t* indices;    // `count` entries allocated in trailing chunk
  vm_obj_payload_t  c_payload;  // cache: resolved payload (ptr, owner, count, type, pad)
};

_Static_assert(sizeof(struct vm_accessor_t) == 20, "accessor header size feeds the RAM budget");

// ===========================================================================
// 2. Helpers (Lookups, Resolution, Conversions, Store Engine)
// ===========================================================================

// --- Registry Lookups & Payload Construction ---

static __always_inline vm_obj_h vm_obj_get_by_id(uint16_t id) {
  return (vm_obj_h)vm_store_get(VM_REG_OBJ, id);
}

static __always_inline vm_accessor_t* vm_accessor_get_by_id(uint16_t id) {
  return (vm_accessor_t*)vm_store_get(VM_REG_ACC, id);
}

/** @brief Reverse lookup: maps object handle to registry ID or VM_ID_NONE.
 * Dynamic objects return (slot | VM_OBJ_ID_DYN_BIT). */
uint16_t vm_obj_get_id(vm_obj_h obj);

/** @brief Universal object lookup by ID, handling both arena and dynamic objects. */
static inline vm_obj_h vm_obj_lookup_by_id(uint16_t id) {
  if (id == VM_ID_NONE) return NULL;
  if (id & VM_OBJ_ID_DYN_BIT) return vm_obj_dyn_get_by_id(id & (uint16_t)~VM_OBJ_ID_DYN_BIT);
  return vm_obj_get_by_id(id);
}

/** @brief Element `i` of an already-resolved payload, bounds-checked. */
static __always_inline vm_obj_payload_t vm_payload_get_at(vm_obj_payload_t p, uint16_t i) {
  if (unlikely(!p.ptr || !vm_type_ok((uint8_t)p.type) || i >= p.count)) {
    return (vm_obj_payload_t){.ptr = NULL, .owner = NULL, .count = 0, .type = VM_OBJ_NONE, ._pad = 0};
  }
  return (vm_obj_payload_t){
      .ptr   = (uint8_t*)p.ptr + ((size_t)i << vm_type_shift((uint8_t)p.type)),
      .owner = p.owner,
      .count = 1,
      .type  = p.type,
  };
}

/** @brief A whole object as a payload. */
static __always_inline vm_obj_payload_t vm_make_payload(vm_obj_h obj) {
  if (unlikely(!obj)) return (vm_obj_payload_t){0};
  uint8_t t = (uint8_t)obj->head.d.obj_t;
  return (vm_obj_payload_t){
      .ptr   = obj->payload,
      .owner = obj,
      .count = (uint16_t)(obj->head.payload_size >> vm_type_shift(t)),
      .type  = t,
  };
}

// --- Resolution Helper (Fast Path) ---

/**
 * @brief Fast inline resolution for cached or shallow literal accessors.
 * Resolves whole objects (count=0), single literal indices (obj[i]), and 2-level
 * literal chains through pointer arrays (box[0][i]) directly without frame setup.
 * Returns false on cache-miss or complex shapes so caller falls back to out-of-line vm_internal_acc_resolve_deep().
 */
static __always_inline bool vm_acc_resolve_fast(const vm_accessor_t* acc, vm_obj_payload_t* out) {
  if (unlikely(!acc)) return false;

  // 1. Cached path: return pre-resolved payload immediately
  if (likely(acc->flags & VM_ACC_F_CACHED)) {
    if (unlikely(!acc->c_payload.ptr)) return false;
    *out = acc->c_payload;
    return true;
  }

  // 2. Fetch root object from store registry
  vm_obj_h obj = vm_obj_get_by_id(acc->id);
  if (unlikely(!obj)) return false;

  uint8_t n = acc->count;
  // Whole-object accessor: payload spans all elements
  if (n == 0) {
    *out = vm_make_payload(obj);
    return true;
  }
  // Deep chains (>2) fall back to recursive vm_internal_acc_resolve_deep()
  if (unlikely(n > 2)) return false;

  const vm_index_t* idx = acc->indices;
  // Dynamic ref (VM_IDX_REF) or tag scan (VM_IDX_NAME) require full walk
  if (unlikely(!idx || idx[0].kind != VM_IDX_LITERAL)) return false;

  uint32_t index = idx[0].value;
  if (unlikely(index > UINT16_MAX)) return false;

  // 3. Two-level chain: traverse intermediate PTR array directly
  if (n == 2) {
    if (unlikely(idx[1].kind != VM_IDX_LITERAL || (uint8_t)obj->head.d.obj_t != VM_OBJ_PTR)) return false;
    uint32_t cell_off = index << 2;
    if (unlikely(cell_off >= obj->head.payload_size)) return false;
    obj = *(vm_obj_h*)(obj->payload + cell_off);
    if (unlikely(!obj)) return false;
    index = idx[1].value;
    if (unlikely(index > UINT16_MAX)) return false;
  }

  // 4. Resolve leaf element: compute byte offset from element type shift
  uint8_t t = (uint8_t)obj->head.d.obj_t;
  if (unlikely(!vm_type_ok(t))) return false;

  uint32_t off = index << vm_type_shift(t);
  if (unlikely(off >= obj->head.payload_size)) return false;

  *out = (vm_obj_payload_t){
      .ptr   = obj->payload + off,
      .owner = obj,
      .count = 1,
      .type  = t,
  };
  return true;
}

#include "vm_obj_access_internal.h"

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
err_h vm_obj_get_payload(vm_obj_payload_t* target, const vm_accessor_t* source);
err_h vm_obj_get_obj(vm_obj_h* target, const vm_accessor_t* source);
/** @brief Object owning the addressed bytes; never follows a trailing PTR. */
err_h vm_obj_get_owner(vm_obj_h* target, const vm_accessor_t* source);
/** @brief Resolve a parent object and find its child by exact byte-length tag.
 * Failure preserves *target. name need not be NUL-terminated. */
err_h vm_obj_get_child(vm_obj_h* target, const vm_accessor_t* parent, const char* name, size_t name_len);

// Write
/** Clear without publishing freshness. Failure still returns an error. */
err_h vm_obj_clear_quiet(vm_obj_h obj);

// Copy & Clone
/** @brief Validate the entire destination, then copy without allocating.
 * Failure preserves destination values and freshness. Requires single-writer
 * execution; overlapping source leaves follow deterministic slot order. */
err_h vm_obj_copy_content(const vm_accessor_t* source, const vm_accessor_t* target);
err_h vm_obj_copy_direct(vm_obj_h src, vm_obj_h dst);

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
err_h vm_obj_clone_shape(vm_obj_h* out, vm_obj_h src);

/** @brief Copy `source` into the pointer cell `target` names, building the
 *  destination first if its schema (including tags) differs. Fill completes
 *  before replacement releases the old tree; failures preserve the old slot.
 *  A matching destination is preflighted and refilled, then its holder published. */
err_h vm_obj_clone_into(const vm_accessor_t* source, const vm_accessor_t* target);

// Link
err_h vm_obj_link(const vm_accessor_t* child, const vm_accessor_t* target);
err_h vm_obj_link_direct(vm_obj_h cell, uint16_t index, vm_obj_h child);

// Update / Freshness Mark
/** @brief Explicit aggregate update marking, after a successful field update.
 * Scalar writes mark their owner only; copy marks the destination subtree;
 * Clone additionally marks its holder. No implicit ancestor propagation. */
err_h vm_obj_mark_updated(vm_obj_h obj);
