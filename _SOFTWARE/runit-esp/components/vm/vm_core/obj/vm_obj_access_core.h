#pragma once

#include "vm_errors.h"
#include "vm_obj.h"
#include "vm_obj_dyn.h"
#include "vm_store.h"

/*
 * Object Access: types and resolution fast path
 *
 * What both vm_obj_access.h (public API) and vm_obj_access_internal.h (engines) build on, so
 * neither has to include the other.
 *
 * Logic Flow:
 *   1. Types & Core Data Structures (vm_obj_payload_t, vm_val_t, vm_accessor_t)
 *   2. Helpers (Object/Store lookups, payload construction, fast resolution)
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

//#ref-enum @alias VM Index Kind
typedef enum vm_index_kind_e {
  VM_IDX_LITERAL = 0,  //@alias Literal Index @description Literal index: a fixed array position
  VM_IDX_REF     = 1,  //@alias Accessor Reference @description Accessor reference: read another accessor to obtain the index
  VM_IDX_NAME    = 2,  //@alias Child Name @description Child name: find a child object by its tag
} vm_index_kind_e;

/** @brief String representation of accessor index kind for debugging. */
static inline const char* vm_index_kind_str(vm_index_kind_e k) {
  return vm_index_kind_name((uint8_t)k);
}

/* One resolved path step, in memory. Its wire form is vm_wire_idx_*_t
   (vm_wire.h); this layout only feeds the arena size. */
typedef struct {
  uint8_t kind;      // vm_index_kind_e
  uint8_t name_len;  // VM_IDX_NAME: length of name
  union {            // by kind
    uint32_t             value;  // VM_IDX_LITERAL
    const vm_accessor_t* ref;    // VM_IDX_REF: resolved at load
    const char*          name;   // VM_IDX_NAME: NUL-terminated copy in the arena
  };
} vm_index_t;

_Static_assert(sizeof(vm_index_t) == 8, "index step size feeds the arena size (OPEN's total_size)");

/* An accessor, in memory. Its wire form is vm_wire_acc_t (vm_wire.h). */
struct vm_accessor_t {
  uint16_t          id;         // root object ID
  uint8_t           count;      // index steps
  uint8_t           flags;      // VM_ACC_F_*
  const vm_index_t* indices;    // count steps, right after the accessor in the arena
  vm_obj_payload_t  c_payload;  // VM_ACC_F_CACHED: the fixed path, resolved at load (never changes: arena objects stay put)
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

// --- Resolution (the hot path) ---

/* Walks an accessor's steps (vm_obj_access_internal.c): every accessor without a cached payload. `depth` counts the
 * REF steps followed into other accessors (CONFIG_VM_ACCESSOR_MAX_DEPTH). */
MUST_USE err_h vm_acc_walk(const vm_accessor_t* acc, uint8_t depth, vm_obj_payload_t* out);

/**
 * @brief Where an accessor points. An accessor whose path is fixed (no steps, or one literal step on its root) was
 * resolved at load (VM_ACC_F_CACHED, vm_accessor_bind) and costs a flag test and a copy; anything else is walked.
 */
static __always_inline MUST_USE err_h vm_acc_resolve(const vm_accessor_t* acc, vm_obj_payload_t* out) {
  if (likely(acc != NULL && (acc->flags & VM_ACC_F_CACHED))) {
    *out = acc->c_payload;
    return NULL;
  }
  return vm_acc_walk(acc, 0, out);
}
