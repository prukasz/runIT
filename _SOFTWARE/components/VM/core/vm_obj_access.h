#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/cdefs.h>
#include "sys_error.h"     // err_h return type for the prototypes below
#include "sys_error_vm.h"  // ERR_VM_*/OWNER_VM_* -- not used in this header itself, kept
                           // so anything that only includes this header still gets them
#include "vm_obj.h"

typedef struct vm_obj_table_t {
  vm_obj_h* items;
  uint16_t count;
} vm_obj_table_t;

typedef struct vm_payload_t {
  vm_obj_t_e type;
  void* ptr;       //->>> make this union of pointers
  uint16_t count;  // number of `type` elements available at ptr
} vm_payload_t;

static inline vm_obj_h vm_obj_get(const vm_obj_table_t* t, uint16_t id) {
  return (id < t->count) ? t->items[id] : NULL;
}

static inline vm_obj_h vm_obj_get_a(vm_accessor_t*)  // gets last possible object that accessor is accessing -> ignores payload

#define OBJ_GET_VAL(val*, accessor_h)  // GENERIC with cast
#define OBJ_SET_VAL(val, accessor_h)   // GENERIC with cast or ceil // floor
                                                     // this can be used inside blocks for auto casting

    err_h obj_copy(obj_source, obj_target);

err_h obj_copy_partial(accessor_source, accessor_target)  // copy whole objects if possible or just item

    typedef struct vm_accessor_t vm_accessor_t;

typedef struct {
  bool is_ref;
  union {
    uint32_t value;            // literal index
    const vm_accessor_t* ref;  // resolve this accessor to get the index, live
  };
} vm_index_t;

struct vm_accessor_t {
  uint16_t id;                // root object's id in the table
  uint8_t count;              // number of chained indices; 0 = whole object
  const vm_index_t* indices;  // caller-owned array (stack), `count` entries
};

// read a resolved ref back as an integer, for use as the next index
static inline uint32_t vm_ref_as_index(vm_ref_t r) {
  if (!r.ptr) return 0;
  switch (r.type) {
    case VM_OBJ_U8:
    case VM_OBJ_B:
      return *(uint8_t*)r.ptr;
    case VM_OBJ_U32:
    case VM_OBJ_PTR:
      return *(uint32_t*)r.ptr;
    case VM_OBJ_I32:
      return (uint32_t)(*(int32_t*)r.ptr);
    case VM_OBJ_U64:
      return (uint32_t)(*(uint64_t*)r.ptr);
    case VM_OBJ_F:
      return (uint32_t)(*(float*)r.ptr);
    default:
      return 0;
  }
}

// caps by_ref recursion depth: a packet-sourced tree isn't trusted to be
// acyclic, and unbounded recursion here means unbounded stack usage
#define VM_ACCESSOR_MAX_DEPTH 8

// Defined in vm_obj_access.c (not inline here) -- heavier/branchier than the
// leaf accessors above, and the one function in this header that actually
// needs an ambient OWNER pervasively, so it gets a real home with
// `#define OWNER OWNER_VM_ACCESSOR` instead of the header-only `_OWNED`
// macro variants. See that file for the definition.
err_h vm_accessor_resolve_d(const vm_obj_table_t* t, const vm_accessor_t* acc, uint8_t depth, vm_ref_t* out_ref);
err_h vm_accessor_resolve(const vm_obj_table_t* t, const vm_accessor_t* acc, vm_ref_t* out_ref);

/*

[linear alocator] allocate root -> [linear alocator] allocate next (nested) accessor if required
assign pointer to root -> execute recurseively
*/

/*
The single active program's object table. Exactly one program is ever
loaded at a time -- arenas are rebuilt whole on reload, never partially
swapped (see vm_alloc.h) -- so every VM_OBJ_GET/VM_OBJ_SET resolves against
this instead of threading a vm_obj_table_t* through every block's get/set
call. Populated by the loader (vm_code.c, not yet written) when a program
is (re)loaded; empty (count = 0) until then, so resolution against it just
fails closed rather than dereferencing garbage.
*/
extern vm_obj_table_t g_vm_obj_table;

// Tagged-union-free scratch value wide enough for any scalar vm_obj_t_e --
// the conversion hub between "whatever's actually stored" and "whatever
// type the caller asked for". Not tagged itself: the ref/accessor's
// resolved type is the tag, passed alongside wherever this is used.
typedef union {
  uint8_t u8;
  uint32_t u32;
  int32_t i32;
  uint64_t u64;
  float f;
} vm_val_t;

// Reads a resolved ref's raw stored value into the union member matching
// its actual type. PTR/STR/NONE aren't single-scalar values -- left zeroed.
static inline vm_val_t vm_ref_read(vm_ref_t r) {
  vm_val_t v = {0};
  switch (r.type) {
    case VM_OBJ_U8:
    case VM_OBJ_B:
      v.u8 = *(uint8_t*)r.ptr;
      break;
    case VM_OBJ_U32:
      v.u32 = *(uint32_t*)r.ptr;
      break;
    case VM_OBJ_I32:
      v.i32 = *(int32_t*)r.ptr;
      break;
    case VM_OBJ_U64:
      v.u64 = *(uint64_t*)r.ptr;
      break;
    case VM_OBJ_F:
      v.f = *(float*)r.ptr;
      break;
    default:
      break;
  }
  return v;
}

// Casts a vm_val_t (tagged by `tag`, a vm_obj_t_e) into *dst_ptr, converting
// if dst_ptr's type doesn't match -- e.g. an object stored as I32 read into
// a float destination. __typeof__ picks the cast target from dst_ptr itself,
// so this works for any scalar dst_ptr type without a second dispatch table.
#define VM_VAL_CAST_TO(dst_ptr, val, tag)               \
  do {                                                  \
    switch (tag) {                                      \
      case VM_OBJ_U8:                                   \
      case VM_OBJ_B:                                    \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))(val).u8;  \
        break;                                          \
      case VM_OBJ_U32:                                  \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))(val).u32; \
        break;                                          \
      case VM_OBJ_I32:                                  \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))(val).i32; \
        break;                                          \
      case VM_OBJ_U64:                                  \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))(val).u64; \
        break;                                          \
      case VM_OBJ_F:                                    \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))(val).f;   \
        break;                                          \
      default:                                          \
        *(dst_ptr) = (__typeof__(*(dst_ptr)))0;         \
        break;                                          \
    }                                                   \
  } while (0)

/*
VM_OBJ_GET(dst_ptr, acc_ptr): resolves `acc_ptr` against the active program
(g_vm_obj_table) and writes the converted result into *dst_ptr. dst_ptr may
be any scalar pointer (uint8_t*, uint32_t*, int32_t*, uint64_t*, float*) --
conversion from whatever's actually stored happens automatically. Evaluates
to an err_h: NULL on success, or the resolve failure chain otherwise (in
which case *dst_ptr is left untouched). This is the "dynamic chain" path --
a fresh vm_accessor_resolve() every call, per the note above.

  uint8_t v;
  err_h err = VM_OBJ_GET(&v, &acc);
  if (err) return SE_WRAP_ERR(err, ERR_VM_BLOCK_INPUT_UNRESOLVED, .block_idx = b, .input_idx = i);

VM_REF_GET(dst_ptr, ref): same conversion, but against an already-resolved
vm_ref_t -- the "constant chain" fast path: resolve once at block-wiring/
link time, cache the vm_ref_t on the block, then every execution cycle uses
this instead of re-walking the accessor chain. No table lookup, no err_h --
the ref was already validated when it was cached.

  vm_ref_t cached = ...;  // resolved once, stored on the block
  float v;
  VM_REF_GET(&v, cached);
*/
#define VM_OBJ_GET(dst_ptr, acc_ptr)                                               \
  ({                                                                               \
    vm_ref_t __vog_ref;                                                            \
    err_h __vog_err = vm_accessor_resolve(&g_vm_obj_table, (acc_ptr), &__vog_ref); \
    if (!__vog_err) {                                                              \
      VM_VAL_CAST_TO((dst_ptr), vm_ref_read(__vog_ref), __vog_ref.type);           \
    }                                                                              \
    __vog_err;                                                                     \
  })

#define VM_REF_GET(dst_ptr, ref) VM_VAL_CAST_TO((dst_ptr), vm_ref_read(ref), (ref).type)
