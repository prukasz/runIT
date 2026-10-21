#pragma once
#include "vm_obj_access_core.h"

/* Shared object engines for the block permission boundary. Not consumer APIs. */
MUST_USE err_h vm_internal_obj_writable(vm_obj_h obj, bool user);
MUST_USE err_h vm_internal_obj_copy_content(const vm_accessor_t* source, const vm_accessor_t* target, bool user);
MUST_USE err_h vm_internal_obj_copy_direct(vm_obj_h src, vm_obj_h dst, bool user);
MUST_USE err_h vm_internal_obj_clone_into(const vm_accessor_t* source, const vm_accessor_t* target, bool user);
MUST_USE err_h vm_internal_obj_link(const vm_accessor_t* child, const vm_accessor_t* target, bool user);

/* Internal tree comparison, clone, and slot manipulation engines */
bool  vm_internal_shape_matches(vm_obj_h a, vm_obj_h b, uint8_t depth, bool schema);
MUST_USE err_h vm_internal_clone_shape(vm_obj_h* out, vm_obj_h src, uint8_t depth);
MUST_USE err_h slot_store(vm_obj_h owner, vm_obj_h* cell, vm_obj_h child);

// --- Inlined indexing & child lookup helpers ---

// one element of obj as a payload, bounds-checked (see vm_obj_get_elem_ptr())
static __always_inline vm_obj_payload_t obj_elem(vm_obj_h obj, uint32_t i) {
  uint8_t* p = vm_obj_get_elem_ptr(obj, i);
  if (unlikely(!p)) return (vm_obj_payload_t){.ptr = NULL, .owner = NULL, .count = 0, .type = VM_OBJ_NONE, ._pad = 0};
  return (vm_obj_payload_t){.ptr = p, .owner = obj, .count = 1, .type = (uint8_t)obj->head.d.obj_t, ._pad = 0};
}

// Index of `parent`'s child (a VM_OBJ_PTR array) tagged `name`, or -1. Linear
// with a compare per child -- see VM_IDX_NAME in the header.
static __always_inline int32_t find_child_by_name(vm_obj_h parent, const char* name, uint8_t n) {
  if (unlikely(!name || n == 0 || n > VM_OBJ_NAME_MAX)) return -1;
  char first_char = name[0];

  uint16_t  cnt      = vm_obj_get_items_cnt(parent);
  vm_obj_h* children = (vm_obj_h*)parent->payload;
  for (uint16_t i = 0; i < cnt; i++) {
    vm_obj_h c = children[i];
    if (unlikely(!c)) continue;  // unwired slot
    uint8_t     tag_len = 0;
    const char* tag     = vm_obj_get_tag(c, &tag_len);
    if (tag == NULL || tag_len != n || tag[0] != first_char) continue;

    uint8_t k = 1;
    while (k < n && tag[k] == name[k]) k++;
    if (k == n) return (int32_t)i;
  }
  return -1;
}

// --- Internal Read & Conversion Helpers ---

/* Only stored scalar types have C entry points. Unsupported C types fail
 * _Generic selection at compile time rather than silently narrowing. */
float    vm_get_as_f32(vm_obj_t_e type, const void* src);
int32_t  vm_get_as_i32(vm_obj_t_e type, const void* src);
uint32_t vm_get_as_u32(vm_obj_t_e type, const void* src);
uint8_t  vm_get_as_u8(vm_obj_t_e type, const void* src);
char     vm_get_as_char(vm_obj_t_e type, const void* src);
bool     vm_get_as_bool(vm_obj_t_e type, const void* src);

/* Errors are returned, never emitted here: the top caller owns reporting. */
MUST_USE err_h vm_internal_scalar_payload_check(vm_obj_payload_t p, vm_obj_h owner, uint16_t id);

// The scalar an accessor points at (its first element): an error for an empty or non-scalar payload.
static __always_inline err_h vm_acc_scalar(const vm_accessor_t* acc, vm_obj_payload_t* out) {
  err_h e = vm_acc_resolve(acc, out);
  if (unlikely(e)) return e;
  if (unlikely(!out->count || !vm_type_is_scalar(out->type))) return vm_internal_scalar_payload_check(*out, out->owner, acc->id);
  return NULL;
}

/* Every conversion clamps before the final in-range C conversion. Float to
 * integer rounds to nearest, halfway away from zero. NaN -> integer zero.
 * Direct-load fast path bypasses function calls when types match. */
#define VM_INTERNAL_LOAD_SCALAR(dst, type, src)                                                             \
  do {                                                                                                      \
    __auto_type __ls_d = (dst);                                                                             \
    const void* __ls_s = (src);                                                                             \
    vm_obj_t_e  __ls_t = (type);                                                                            \
    *__ls_d            = _Generic(*__ls_d,                                                                  \
        float: likely(__ls_t == VM_OBJ_F) ? *(const float*)__ls_s : vm_get_as_f32(__ls_t, __ls_s),          \
        uint32_t: likely(__ls_t == VM_OBJ_U32) ? *(const uint32_t*)__ls_s : vm_get_as_u32(__ls_t, __ls_s),  \
        int32_t: likely(__ls_t == VM_OBJ_I32) ? *(const int32_t*)__ls_s : vm_get_as_i32(__ls_t, __ls_s),    \
        uint8_t: likely(__ls_t == VM_OBJ_U8) ? *(const uint8_t*)__ls_s : vm_get_as_u8(__ls_t, __ls_s),      \
        bool: likely(__ls_t == VM_OBJ_B) ? (*(const uint8_t*)__ls_s != 0) : vm_get_as_bool(__ls_t, __ls_s), \
        char: likely(__ls_t == VM_OBJ_STR) ? *(const char*)__ls_s : vm_get_as_char(__ls_t, __ls_s));        \
  } while (0)

// --- Type & Value Boxing Helpers ---

#define VM_TYPE_OF(x) _Generic((x), uint8_t: VM_OBJ_U8, char: VM_OBJ_STR, uint32_t: VM_OBJ_U32, int32_t: VM_OBJ_I32, float: VM_OBJ_F, bool: VM_OBJ_B)

#define VM_VAL_OF(x) \
  _Generic((x), \
      float:    (vm_val_t){.f = (float)(x)}, \
      uint32_t: (vm_val_t){.u32 = (uint32_t)(x)}, \
      int32_t:  (vm_val_t){.i32 = (int32_t)(x)}, \
      uint8_t:  (vm_val_t){.u8 = (uint8_t)(x)}, \
      bool:     (vm_val_t){.u8 = (uint8_t)(x)}, \
      char:     (vm_val_t){.u8 = (uint8_t)(x)})

// --- Scalar store ---

/* Stores `v` (of type `src_type`, every member of vm_val_t starts at its address) into the scalar of type `type` at
 * `ptr` in `owner`, converted. `src_type` is a constant at every call (VM_TYPE_OF), so the same type folds to a plain
 * store. */
static __always_inline err_h vm_internal_store(vm_obj_h owner, void* ptr, uint8_t type, vm_val_t v, vm_obj_t_e src_type) {
  switch (type) {
    case VM_OBJ_F:   VM_INTERNAL_LOAD_SCALAR((float*)ptr, src_type, &v);    break;
    case VM_OBJ_U32: VM_INTERNAL_LOAD_SCALAR((uint32_t*)ptr, src_type, &v); break;
    case VM_OBJ_I32: VM_INTERNAL_LOAD_SCALAR((int32_t*)ptr, src_type, &v);  break;
    case VM_OBJ_U8:
    case VM_OBJ_STR: VM_INTERNAL_LOAD_SCALAR((uint8_t*)ptr, src_type, &v);  break;
    case VM_OBJ_B:   VM_INTERNAL_LOAD_SCALAR((bool*)ptr, src_type, &v);     break;
    default:         return vm_obj_not_scalar_err(vm_obj_get_id(owner), (vm_obj_t_e)type, VM_ID_NONE);
  }
  return NULL;
}

// A block's write to an object it produces (an output): its first element; usr_protected does not apply.
static __always_inline err_h vm_internal_set_scalar(vm_obj_h obj, vm_val_t v, vm_obj_t_e src_type) {
  if (unlikely(obj == NULL)) return vm_obj_null_obj_err();
  if (unlikely(!obj->head.f.mutable)) return vm_obj_not_mutable_err(vm_obj_get_id(obj));
  return vm_internal_store(obj, obj->payload, obj->head.d.obj_t, v, src_type);  // never empty (vm_obj_shape)
}

// A user's write (SET) through an accessor: the target must be mutable and not usr_protected.
static __always_inline err_h vm_internal_set_scalar_usr(const vm_accessor_t* acc, vm_val_t v, vm_obj_t_e src_type) {
  vm_obj_payload_t p;
  err_h e = vm_acc_resolve(acc, &p);
  if (unlikely(e)) return e;
  if (unlikely(!p.owner->head.f.mutable || p.owner->head.f.usr_protected)) return vm_internal_obj_writable(p.owner, true);
  if (unlikely(!p.count)) return vm_obj_oob_err(vm_obj_get_id(p.owner), 0);
  return vm_internal_store(p.owner, p.ptr, p.type, v, src_type);
}
