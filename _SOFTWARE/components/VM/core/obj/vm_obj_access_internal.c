#include "vm_obj_access_internal.h"

#include <string.h>

#include "vm_obj_dyn.h"

#define OWNER OWNER_VM_ACCESSOR

/*
 * Object Access & Resolution Internal Engine
 *
 * Logic Flow:
 *   1. Permission validation (vm_internal_obj_writable)
 *   2. Read & index conversion (vm_get_as_*, payload_as_index)
 *   3. Accessor resolution walk (vm_internal_acc_resolve_deep, vm_internal_obj_resolve)
 *   4. Store conversion & dynamic slot management (boxed_ptr, vm_internal_store_converted, slot_store)
 *   5. Tree comparison, copy, clone, and link engines (copy_values, copy_tree, vm_internal_shape_matches, vm_internal_obj_copy_content, vm_internal_obj_copy_direct, vm_internal_clone_shape, vm_internal_obj_clone_into, vm_internal_obj_link)
 *   6. Internal scalar setter (vm_internal_obj_set_scalar_at)
 */

// ===========================================================================
// 1. Permission Validation
// ===========================================================================

err_h vm_internal_obj_writable(vm_obj_h obj, bool user) {
  if (!obj) return vm_obj_null_obj_err();
  if (!obj->head.f.mutable) return vm_obj_not_mutable_err(obj);
  if (user && obj->head.f.usr_protected) {
    SE_RET_ERR(ERR_VM_OBJ_USR_PROTECTED, .obj_id = vm_obj_get_id(obj));
  }
  return NULL;
}

// ===========================================================================
// 2. Read & Index Conversions
// ===========================================================================

float vm_get_as_f32(vm_obj_t_e type, const void* src) {
  switch (type) {
    case VM_OBJ_U8: case VM_OBJ_B: case VM_OBJ_STR: return *(const uint8_t*)src;
    case VM_OBJ_U32: return (float)*(const uint32_t*)src;
    case VM_OBJ_I32: return (float)*(const int32_t*)src;
    case VM_OBJ_F: return *(const float*)src;
    default: return 0.0f;
  }
}

int32_t vm_get_as_i32(vm_obj_t_e type, const void* src) {
  switch (type) {
    case VM_OBJ_U8: case VM_OBJ_B: case VM_OBJ_STR: return *(const uint8_t*)src;
    case VM_OBJ_U32: {
      uint32_t v = *(const uint32_t*)src;
      return v > INT32_MAX ? INT32_MAX : (int32_t)v;
    }
    case VM_OBJ_I32: return *(const int32_t*)src;
    case VM_OBJ_F: {
      float v = roundf(*(const float*)src);
      if (isnan(v)) return 0;
      if (v >= 0x1p31f) return INT32_MAX;
      if (v <= -0x1p31f) return INT32_MIN;
      return (int32_t)v;
    }
    default: return 0;
  }
}

uint32_t vm_get_as_u32(vm_obj_t_e type, const void* src) {
  switch (type) {
    case VM_OBJ_U8: case VM_OBJ_B: case VM_OBJ_STR: return *(const uint8_t*)src;
    case VM_OBJ_U32: return *(const uint32_t*)src;
    case VM_OBJ_I32: {
      int32_t v = *(const int32_t*)src;
      return v < 0 ? 0 : (uint32_t)v;
    }
    case VM_OBJ_F: {
      float v = roundf(*(const float*)src);
      if (isnan(v) || v <= 0) return 0;
      if (v >= 0x1p32f) return UINT32_MAX;
      return (uint32_t)v;
    }
    default: return 0;
  }
}

uint8_t vm_get_as_u8(vm_obj_t_e type, const void* src) {
  switch (type) {
    case VM_OBJ_U8: case VM_OBJ_B: case VM_OBJ_STR: return *(const uint8_t*)src;
    case VM_OBJ_U32: {
      uint32_t v = *(const uint32_t*)src;
      return v > UINT8_MAX ? UINT8_MAX : (uint8_t)v;
    }
    case VM_OBJ_I32: {
      int32_t v = *(const int32_t*)src;
      return v < 0 ? 0 : v > UINT8_MAX ? UINT8_MAX : (uint8_t)v;
    }
    case VM_OBJ_F: {
      float v = roundf(*(const float*)src);
      if (isnan(v) || v <= 0.0f) return 0;
      if (v >= 255.0f) return UINT8_MAX;
      return (uint8_t)v;
    }
    default: return 0;
  }
}

char vm_get_as_char(vm_obj_t_e type, const void* src) {
  switch (type) {
    case VM_OBJ_STR: case VM_OBJ_B: case VM_OBJ_U8: return *(const char*)src;
    case VM_OBJ_U32: {
      uint32_t v = *(const uint32_t*)src;
      return v > (uint32_t)CHAR_MAX ? CHAR_MAX : (char)v;
    }
    case VM_OBJ_I32: {
      int32_t v = *(const int32_t*)src;
      return v < CHAR_MIN ? CHAR_MIN : v > CHAR_MAX ? CHAR_MAX : (char)v;
    }
    case VM_OBJ_F: {
      float v = roundf(*(const float*)src);
      if (isnan(v)) return 0;
      if (v >= (float)CHAR_MAX) return CHAR_MAX;
      if (v <= (float)CHAR_MIN) return CHAR_MIN;
      return (char)v;
    }
    default: return 0;
  }
}

bool vm_get_as_bool(vm_obj_t_e type, const void* src) {
  switch (type) {
    case VM_OBJ_B: case VM_OBJ_U8: case VM_OBJ_STR: return *(const uint8_t*)src != 0;
    case VM_OBJ_U32: return *(const uint32_t*)src != 0;
    case VM_OBJ_I32: return *(const int32_t*)src != 0;
    case VM_OBJ_F: return *(const float*)src != 0.0f;
    default: return false;
  }
}

err_h vm_internal_scalar_payload_check(vm_obj_payload_t p, vm_obj_h owner, uint16_t id) {
  if (!p.ptr || !p.count) SE_RET_ERR(ERR_VM_OBJ_EMPTY, .type = p.type);
  if (!vm_type_is_scalar(p.type)) return vm_obj_not_scalar_err(owner, (vm_obj_t_e)p.type, id);
  return NULL;
}

static err_h payload_as_index(uint32_t* out, vm_obj_payload_t p, const vm_accessor_t* acc) {
  SE_RET_IF_ERR(vm_internal_scalar_payload_check(p, p.owner, acc->id));
  /* Negative indices are invalid, not scalar values to clamp to element zero. */
  if ((p.type == VM_OBJ_I32 && *(const int32_t*)p.ptr < 0) ||
      (p.type == VM_OBJ_F && (!isfinite(*(const float*)p.ptr) || *(const float*)p.ptr < 0)))
    return vm_err_chain_oob(acc->id, acc->count, UINT32_MAX, p.owner);
  *out = vm_get_as_u32((vm_obj_t_e)p.type, p.ptr);
  return NULL;
}

// ===========================================================================
// 3. Accessor Resolution Walk
// ===========================================================================

/* Everything vm_acc_resolve_fast() misses funnels through vm_internal_acc_resolve_deep(), which walks
   an accessor's index chain and returns both the payload and its owner object
   -- only the walk itself has both in hand at once. */

// Resolution walks the accessor and returns the payload with its owner;
// mutability/permission gating is handled at the mutating operations.
err_h vm_internal_acc_resolve_deep(const vm_accessor_t* acc, uint8_t depth, vm_obj_payload_t* out) {
  SE_CHECK_NOT_NULL(acc);
  if (acc->flags & VM_ACC_F_CACHED) {
    if (!acc->c_payload.owner) return vm_err_null_obj(acc->id, acc->count, NULL);
    if (!acc->c_payload.ptr || !acc->c_payload.count) SE_RET_ERR(ERR_VM_OBJ_EMPTY, .type = acc->c_payload.type);
    if (!vm_type_ok(acc->c_payload.type)) SE_RET_ERR(ERR_VM_OBJ_BAD_TYPE, .type = acc->c_payload.type);
  }
  if (acc->count) SE_CHECK_NOT_NULL(acc->indices);
  *out = (vm_obj_payload_t){.ptr = NULL, .owner = NULL, .count = 0, .type = VM_OBJ_NONE, ._pad = 0};

  if (unlikely(depth >= VM_ACCESSOR_MAX_DEPTH)) return vm_err_depth(acc->id);

  vm_obj_h obj = vm_obj_get_by_id(acc->id);
  if (unlikely(!obj)) return vm_err_unknown_id(acc->id);

  vm_obj_payload_t p = vm_make_payload(obj);
  for (uint8_t i = 0; i < acc->count; i++) {
    const vm_index_t* idx = &acc->indices[i];
    uint32_t index = UINT32_MAX;
    switch (idx->kind) {
      case VM_IDX_REF: {
        // Try the non-recursive fast path first: it's the common case
        // (object(9), a step-table element) and the recursive call is the
        // expensive part of a by-ref chain, not the read. The depth check
        // comes first because the fast path resolves a level without a
        // frame -- letting it take the last level would let MAX_DEPTH+1
        // fit in MAX_DEPTH frames.
        vm_obj_payload_t sub;
        err_h e = NULL;
        if (!(depth + 1 < VM_ACCESSOR_MAX_DEPTH && vm_acc_resolve_fast(idx->ref, &sub)))
          e = vm_internal_acc_resolve_deep(idx->ref, depth + 1, &sub);
        if (!e) e = payload_as_index(&index, sub, idx->ref);
        if (unlikely(e)) return vm_err_index_failed(e, acc->id, i);
        break;
      }
      case VM_IDX_NAME: {
        SE_CHECK_NOT_NULL(idx->name);
        // checked against `obj`, not `p`: past the first step `p` still
        // describes the previous element, while `obj` is what's about to
        // be indexed, and only a PTR array has tagged children
        if (unlikely((vm_obj_t_e)obj->head.d.obj_t != VM_OBJ_PTR)) {
          return vm_err_expected_ptr(acc->id, i, obj->head.d.obj_t, obj);
        }
        int32_t found = find_child_by_name(obj, idx->name, idx->name_len);
        if (unlikely(found < 0)) {
          return vm_err_name_not_found(acc->id, i, idx->name, idx->name_len);
        }
        index = (uint32_t)found;
        break;
      }
      case VM_IDX_LITERAL:
        index = idx->value;
        break;
      default:
        SE_RET_ERR(ERR_VM_ACC_BAD_KIND, .acc_id = acc->id, .pos = i, .kind = idx->kind);
    }

    p = obj_elem(obj, index);
    if (unlikely(!p.ptr)) return vm_err_chain_oob(acc->id, i, index, obj);

    if (i + 1 < acc->count) {
      if (unlikely(p.type != VM_OBJ_PTR)) return vm_err_expected_ptr(acc->id, i, (uint8_t)p.type, obj);
      vm_obj_h child = *(vm_obj_h*)p.ptr;
      if (unlikely(!child)) {
        return vm_err_null_obj(acc->id, i, obj);  // untrusted-input counterpart to TYPE_MISMATCH: a packet-sourced tree isn't guaranteed fully wired
      }
      obj = child;
    }
  }

  *out = p;
  out->owner = obj;
  return NULL;
}

err_h vm_internal_obj_resolve(const vm_accessor_t* acc, vm_obj_payload_t* out) {
  if (likely(vm_acc_resolve_fast(acc, out))) return NULL;
  return vm_internal_acc_resolve_deep(acc, 0, out);
}

// ===========================================================================
// 4. Store Conversion & Dynamic Slot Management
// ===========================================================================

static const void* boxed_ptr(const vm_val_t* v, vm_obj_t_e type) {
  switch (type) {
    case VM_OBJ_U8: case VM_OBJ_B: case VM_OBJ_STR: return &v->u8;
    case VM_OBJ_U32: return &v->u32;
    case VM_OBJ_I32: return &v->i32;
    case VM_OBJ_F: return &v->f;
    default: return NULL;
  }
}

err_h vm_internal_store_converted(vm_obj_h owner, vm_obj_payload_t slot, vm_val_t v, vm_obj_t_e src_type, uint16_t err_id) {
  const void* src = boxed_ptr(&v, src_type);
  if (!src) return vm_obj_not_scalar_err(owner, src_type, err_id);
  switch (slot.type) {
    case VM_OBJ_B: {
      bool value;
      VM_INTERNAL_LOAD_SCALAR(&value, src_type, src);
      *(uint8_t*)slot.ptr = value;
      break;
    }
    case VM_OBJ_U8:
    case VM_OBJ_STR:
      VM_INTERNAL_LOAD_SCALAR((uint8_t*)slot.ptr, src_type, src);
      break;
    case VM_OBJ_U32:
      VM_INTERNAL_LOAD_SCALAR((uint32_t*)slot.ptr, src_type, src);
      break;
    case VM_OBJ_I32:
      VM_INTERNAL_LOAD_SCALAR((int32_t*)slot.ptr, src_type, src);
      break;
    case VM_OBJ_F:
      VM_INTERNAL_LOAD_SCALAR((float*)slot.ptr, src_type, src);
      break;
    default:
      return vm_obj_not_scalar_err(owner, slot.type, err_id);
  }
  owner->head.f.upd = 1;
  return NULL;
}

/*
A pointer slot is the only place an object is *owned*, so it is the only place
a reference count moves -- which is why both link entry points write through
here rather than storing the handle themselves.

The new child is retained before the old one is released. A program re-linking
a cell to something that lives inside the tree it is replacing would otherwise
free the subtree the new handle points into, one statement before installing
it. Both calls fall straight out on an arena object (one bit -- see
vm_obj_dyn_get_id), so a program's whole load-time wiring pays nothing for this.
*/
err_h slot_store(vm_obj_h owner, vm_obj_h* cell, vm_obj_h child) {
  vm_obj_h prev = *cell;
  if (prev == child) return NULL;  // already there: no churn, and no news to publish
  SE_RET_IF_ERR(vm_obj_dyn_check_link(owner, cell, child));

  vm_obj_dyn_retain(child);
  *cell = child;
  vm_obj_dyn_release(prev);

  owner->head.f.upd = 1;
  return NULL;
}

// ===========================================================================
// 5. Tree Copy, Clone & Comparison Engine
// ===========================================================================

/* One payload of plain values. `s.ptr == d.ptr` is a copy onto itself, which
   is a no-op rather than an error -- and skipping it also keeps memcpy() off
   a source and destination that are the same bytes. */
static err_h copy_values(vm_obj_payload_t s, vm_obj_payload_t d, vm_obj_h d_owner, bool commit) {
  uint8_t w = vm_type_width((vm_obj_t_e)s.type);
  if (unlikely(s.type != d.type || s.count != d.count || w == 0)) {
    SE_RET_ERR(ERR_VM_OBJ_COPY_MISMATCH, .src_type = s.type, .dst_type = d.type, .src_size = s.count, .dst_size = d.count);
  }
  if (commit) {
    if (likely(s.ptr != d.ptr)) memmove(d.ptr, s.ptr, (size_t)w * s.count);
    d_owner->head.f.upd = 1;
  }
  return NULL;
}

/*
A pointer array is copied by walking it, never by copying its bytes. Those
bytes are addresses into this program's arena, so duplicating them would leave
both trees sharing one set of children -- and an edit made through the copy
would then show up in the original, at some unrelated point in the program,
which is the kind of bug nobody traces back to a Copy block. Nothing below
ever writes a pointer, so that cannot happen: the two trees keep their own
children and only values move between them.

The destination supplies the shape. Its children must already exist and match
the source's, which is what lets a deep copy allocate nothing -- it is a walk
over two structures that are already there, not a clone. A destination shaped
differently from its source is a wiring error and says so.
*/
static err_h copy_tree(vm_obj_payload_t s, vm_obj_payload_t d, vm_obj_h d_owner, uint8_t depth, bool commit, bool user) {
  SE_RET_IF_ERR(vm_internal_obj_writable(d_owner, user));
  if (likely(s.type != VM_OBJ_PTR && d.type != VM_OBJ_PTR)) return copy_values(s, d, d_owner, commit);

  if (unlikely(s.type != d.type || s.count != d.count)) {
    SE_RET_ERR(ERR_VM_OBJ_COPY_MISMATCH, .src_type = s.type, .dst_type = d.type, .src_size = s.count, .dst_size = d.count);
  }
  if (unlikely(depth >= VM_OBJ_COPY_MAX_DEPTH)) {
    SE_RET_ERR(ERR_VM_OBJ_COPY_SHAPE, .index = 0, .depth = depth, .reason = VM_COPY_SHAPE_DEPTH);
  }

  vm_obj_h* sc = (vm_obj_h*)s.ptr;
  vm_obj_h* dc = (vm_obj_h*)d.ptr;
  for (uint16_t i = 0; i < s.count; i++) {
    if (sc[i] == dc[i]) continue;  // both unwired, or literally the same child
    if (unlikely(!sc[i] || !dc[i])) {
      SE_RET_ERR(ERR_VM_OBJ_COPY_SHAPE, .index = i, .depth = depth, .reason = sc[i] ? VM_COPY_SHAPE_DST_EMPTY : VM_COPY_SHAPE_SRC_EMPTY);
    }
    // checked per child: the walk writes into each of them, and mutability is
    // a property of the object, not of the accessor that reached its root
    SE_RET_IF_ERR(copy_tree(vm_make_payload(sc[i]), vm_make_payload(dc[i]), dc[i], (uint8_t)(depth + 1), commit, user));
  }

  /* The parent's own bytes did not change, but what hangs under it did, and a
     block whose accessor names the whole table has no other place to see that. */
  if (commit) d_owner->head.f.upd = 1;
  return NULL;
}

/* Same walk as copy_tree, asking only whether the two structures agree.
   payload_size carries the element count for a known type, so one compare
   covers both. Unwired slots must line up too: a source child with no
   destination child to receive it is a different shape, not a copy. */
/* Same walk as copy_tree, asking only whether the two structures agree.
   payload_size carries the element count for a known type, so one compare
   covers both. Unwired slots must line up too: a source child with no
   destination child to receive it is a different shape, not a copy. */
bool vm_internal_shape_matches(vm_obj_h a, vm_obj_h b, uint8_t depth, bool schema) {
  if (!a || !b) return a == b;
  if (a->head.d.obj_t != b->head.d.obj_t || a->head.payload_size != b->head.payload_size) return false;
  if (schema) {
    if (a->head.f.tagged != b->head.f.tagged || a->head.d.name_size != b->head.d.name_size) return false;
    if (a->head.d.name_size && memcmp(a->payload + a->head.payload_size,
                                     b->payload + b->head.payload_size, a->head.d.name_size)) return false;
  }
  if ((vm_obj_t_e)a->head.d.obj_t != VM_OBJ_PTR) return true;
  if (unlikely(depth >= VM_OBJ_COPY_MAX_DEPTH)) return false;

  vm_obj_h* ka = (vm_obj_h*)a->payload;
  vm_obj_h* kb = (vm_obj_h*)b->payload;
  uint16_t n = vm_obj_get_items_cnt(a);
  for (uint16_t i = 0; i < n; i++) {
    if (!vm_internal_shape_matches(ka[i], kb[i], (uint8_t)(depth + 1), schema)) return false;
  }
  return true;
}

err_h vm_internal_obj_copy_content(const vm_accessor_t* source, const vm_accessor_t* target, bool user) {
  vm_obj_payload_t src, dst;
  SE_RET_IF_ERR(vm_internal_obj_resolve(source, &src));
  SE_RET_IF_ERR(vm_internal_obj_resolve(target, &dst));
  SE_RET_IF_ERR(copy_tree(src, dst, dst.owner, 0, false, user));
  return copy_tree(src, dst, dst.owner, 0, true, user);
}

err_h vm_internal_obj_copy_direct(vm_obj_h src, vm_obj_h dst, bool user) {
  if (unlikely(!src || !dst)) return vm_obj_null_obj_err();
  vm_obj_payload_t s = vm_make_payload(src);
  vm_obj_payload_t d = vm_make_payload(dst);
  SE_RET_IF_ERR(copy_tree(s, d, dst, 0, false, user));
  return copy_tree(s, d, dst, 0, true, user);
}

/*
Build a tree shaped like `src` with its values left zero.

Every object it makes is dynamic, because the arena cannot free and this one
has to be replaceable -- a Clone whose source changes shape drops the previous
tree, and a bump allocator has no way to take it back. That is the whole reason
vm_obj_dyn exists, and this is its first caller outside the self test.

Three flags are the clone's own rather than the source's. `mutable`, because a
destination that cannot be written is useless. `retentive` cleared, because
this lives on the heap and is gone at the next load, so NVS has no business
with it. `upd_resetable` set, so the sweep withdraws its freshness at the end
of the pass that filled it -- a snapshot is news once, not forever.
*/
err_h vm_internal_clone_shape(vm_obj_h* out, vm_obj_h src, uint8_t depth) {
  *out = NULL;
  if (unlikely(src->head.d.obj_t == VM_OBJ_PTR && depth >= VM_OBJ_COPY_MAX_DEPTH)) {
    SE_RET_ERR(ERR_VM_OBJ_COPY_SHAPE, .index = 0, .depth = depth, .reason = VM_COPY_SHAPE_DEPTH);
  }

  vm_obj_head_t h = src->head;
  h.f.mutable = 1;
  h.f.usr_protected = 0;  // the snapshot belongs to its caller, not the source producer
  h.f.retentive = 0;
  h.f.upd_resetable = 1;

  uint8_t name_len = 0;
  const char* name = vm_obj_get_tag(src, &name_len);

  vm_obj_h o = NULL;
  SE_RET_IF_ERR(vm_obj_dyn_create(&o, &h, name));

  if ((vm_obj_t_e)h.d.obj_t == VM_OBJ_PTR) {
    vm_obj_h* sk = (vm_obj_h*)src->payload;
    vm_obj_h* dk = (vm_obj_h*)o->payload;
    uint16_t n = vm_obj_get_items_cnt(src);
    for (uint16_t i = 0; i < n; i++) {
      if (!sk[i]) continue;  // unwired in the source, unwired in the copy
      vm_obj_h kid = NULL;
      err_h e = vm_internal_clone_shape(&kid, sk[i], (uint8_t)(depth + 1));
      if (unlikely(e)) {
        /* Half-built, and owned by nothing: releasing the root takes every
           child with it, so a rejected clone leaks neither a slot nor a byte
           -- the same rule vm_obj_create() follows for a rejected shape. */
        vm_obj_dyn_release(o);
        return e;
      }
      dk[i] = kid;
      vm_obj_dyn_retain(kid);
    }
  }

  *out = o;
  return NULL;
}

err_h vm_internal_obj_clone_into(const vm_accessor_t* source, const vm_accessor_t* target, bool user) {
  /* vm_obj_get_obj(), not a payload resolve: a Clone wants the object behind the
     source, header and all, because the header is the shape it has to match. */
  vm_obj_h src = NULL;
  SE_RET_IF_ERR(vm_obj_get_obj(&src, source));

  vm_obj_payload_t slot;
  SE_RET_IF_ERR(vm_internal_obj_resolve(target, &slot));
  SE_RET_IF_ERR(vm_internal_obj_writable(slot.owner, user));
  if (unlikely(slot.type != VM_OBJ_PTR || !slot.ptr)) {
    return vm_err_expected_ptr(target->id, target->count, (uint8_t)slot.type, slot.owner);
  }
  vm_obj_h* cell = (vm_obj_h*)slot.ptr;

  /* The allocation-free case, and the one a running program is normally in:
     the destination already has the right shape, so nothing is built and this
     is exactly what a Set does. Allocation happens on the first pass, and
     afterwards only when the source's shape actually changes. */
  if (unlikely(!vm_internal_shape_matches(src, *cell, 0, true))) {
    vm_obj_h fresh = NULL;
    SE_RET_IF_ERR(vm_internal_clone_shape(&fresh, src, 0));
    // Fill before publication: src may live inside the tree being replaced.
    err_h e = copy_tree(vm_make_payload(src), vm_make_payload(fresh), fresh, 0, true, user);
    if (!e) e = slot_store(slot.owner, cell, fresh);
    if (e) vm_obj_dyn_release(fresh);
    return e;
  }

  SE_RET_IF_ERR(copy_tree(vm_make_payload(src), vm_make_payload(*cell), *cell, 0, false, user));
  SE_RET_IF_ERR(copy_tree(vm_make_payload(src), vm_make_payload(*cell), *cell, 0, true, user));
  slot.owner->head.f.upd = 1;  // a refilled snapshot is a publication too
  return NULL;
}

err_h vm_internal_obj_link(const vm_accessor_t* child, const vm_accessor_t* target, bool user) {
  vm_obj_h c = NULL;
  SE_RET_IF_ERR(vm_obj_get_obj(&c, child));

  vm_obj_payload_t slot;
  SE_RET_IF_ERR(vm_internal_obj_resolve(target, &slot));
  SE_RET_IF_ERR(vm_internal_obj_writable(slot.owner, user));

  if (unlikely(slot.type != VM_OBJ_PTR || !slot.ptr)) {
    return vm_err_expected_ptr(target->id, target->count, (uint8_t)slot.type, slot.owner);
  }

  return slot_store(slot.owner, (vm_obj_h*)slot.ptr, c);
}

// ===========================================================================
// 6. Internal Scalar Setter
// ===========================================================================

err_h vm_internal_obj_set_scalar_at(const vm_accessor_t* target, uint32_t index, vm_val_t v, vm_obj_t_e type, bool user) {
  vm_obj_payload_t p;
  SE_RET_IF_ERR(vm_internal_obj_resolve(target, &p));
  SE_RET_IF_ERR(vm_internal_obj_writable(p.owner, user));
  if (index >= p.count) return vm_err_chain_oob(target->id, target->count, index, p.owner);
  return vm_internal_store_inline(p.owner, vm_payload_get_at(p, (uint16_t)index), v, type, target->id);
}
