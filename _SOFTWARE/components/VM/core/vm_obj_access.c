#include "vm_obj_access.h"
#include <string.h>
#include "esp_compiler.h"  // likely()/unlikely() -- used pervasively below

#define OWNER OWNER_VM_ACCESSOR

vm_obj_table_t g_vm_obj_table = {0};
vm_accessor_table_t g_vm_accessor_table = {0};

/*
Everything that misses vm_resolve_fast() funnels through resolve_d(). It walks
an accessor's index chain and reports both what it landed on (the payload) and
which object that payload lives in (the owner) -- callers need one or the
other, and only the walk itself has both in hand at the same time.
*/

/* One element of obj as a payload, bounds-checked -- see vm_obj_elem_ptr()
   for why this is offset arithmetic rather than a count and a multiply. */
static __always_inline vm_payload_t obj_elem(vm_obj_h obj, uint32_t i) {
  uint8_t* p = vm_obj_elem_ptr(obj, i);
  if (unlikely(!p)) return (vm_payload_t){VM_OBJ_NONE, NULL, 0};
  return (vm_payload_t){(vm_obj_t_e)obj->head.d.obj_t, p, 1};
}

/* ---------------------------------------------------------------------------
   Read conversion -- one copy of each, shared by every pin in the program.
   See the VM_LOAD_CAST_TO note in the header for why these are not inlined.
   `src` is assumed non-NULL and to point at an element of `type`.
   --------------------------------------------------------------------------- */

float vm_read_as_f32(vm_obj_t_e type, const void* src) {
  switch (type) {
    case VM_OBJ_U8:
    case VM_OBJ_B:
    case VM_OBJ_STR:
      return (float)*(const uint8_t*)src;
    case VM_OBJ_U32:
      return (float)*(const uint32_t*)src;
    case VM_OBJ_I32:
      return (float)*(const int32_t*)src;
    case VM_OBJ_U64: {
      uint64_t v;
      memcpy(&v, src, sizeof(v));  // payload is 4-aligned, this read is 8 wide
      return (float)v;
    }
    case VM_OBJ_F:
      return *(const float*)src;
    default:  // PTR/NONE aren't scalars
      return 0.0f;
  }
}

int32_t vm_read_as_i32(vm_obj_t_e type, const void* src) {
  switch (type) {
    case VM_OBJ_U8:
    case VM_OBJ_B:
    case VM_OBJ_STR:
      return (int32_t)*(const uint8_t*)src;
    case VM_OBJ_U32:
      return (int32_t)*(const uint32_t*)src;
    case VM_OBJ_I32:
      return *(const int32_t*)src;
    case VM_OBJ_U64: {
      uint64_t v;
      memcpy(&v, src, sizeof(v));
      return (int32_t)v;
    }
    case VM_OBJ_F:
      // rounds and saturates -- a plain cast here would be UB, and reachable
      // from ordinary user wiring rather than only from corruption
      return (int32_t)vm_f_to_i(*(const float*)src);
    default:
      return 0;
  }
}

int64_t vm_read_as_i64(vm_obj_t_e type, const void* src) {
  switch (type) {
    case VM_OBJ_U8:
    case VM_OBJ_B:
    case VM_OBJ_STR:
      return (int64_t)*(const uint8_t*)src;
    case VM_OBJ_U32:
      return (int64_t)*(const uint32_t*)src;
    case VM_OBJ_I32:
      return (int64_t)*(const int32_t*)src;
    case VM_OBJ_U64: {
      uint64_t v;
      memcpy(&v, src, sizeof(v));
      return (int64_t)v;
    }
    case VM_OBJ_F:
      return vm_f_to_i(*(const float*)src);
    default:
      return 0;
  }
}

// read a resolved payload back as an integer, for use as the next index
static __always_inline uint32_t payload_as_index(vm_payload_t p) {
  if (unlikely(!p.ptr)) return 0;
  vm_val_t v = vm_payload_read(p);
  switch (p.type) {
    case VM_OBJ_U8:
    case VM_OBJ_B:
    case VM_OBJ_STR:
      return v.u8;
    case VM_OBJ_U32:
      return v.u32;
    case VM_OBJ_I32:
      return (uint32_t)v.i32;
    case VM_OBJ_U64:
      return (uint32_t)v.u64;
    case VM_OBJ_F:
      return (uint32_t)vm_f_to_i(v.f);
    default:
      return 0;
  }
}

/*
Find the child of `parent` (a VM_OBJ_PTR array) whose tag matches `name`.
Returns its index, or -1 for no match. Linear with a compare per child --
see the VM_IDX_NAME note in vm_obj_access.h for why that is the right cost
here and where it would be the wrong one.
*/
static __always_inline int32_t find_child_by_name(vm_obj_h parent, const char* name, uint8_t n) {
  if (unlikely(n == 0 || n > VM_OBJ_NAME_MAX)) return -1;
  char first_char = name[0];

  uint16_t cnt = vm_obj_items_cnt(parent);
  vm_obj_h* children = (vm_obj_h*)parent->payload;
  for (uint16_t i = 0; i < cnt; i++) {
    vm_obj_h c = children[i];
    if (unlikely(!c)) continue;  // unwired slot
    uint8_t tag_len = 0;
    const char* tag = vm_obj_tag(c, &tag_len);
    if (tag && tag_len == n && tag[0] == first_char && memcmp(tag + 1, name + 1, n - 1) == 0) return (int32_t)i;
  }
  return -1;
}

/* Built by hand rather than through SE_RET_ERR because the payload carries a
   copied string -- designated initialisers can't fill a char array. */
static err_h name_not_found_err(uint16_t id, uint8_t chain_pos, const char* name) {
  err_h e = SE_ERR_NEW(ERR_VM_ACCESSOR_NAME_NOT_FOUND, .id = id, .chain_pos = chain_pos);
  err_payload_ERR_VM_ACCESSOR_NAME_NOT_FOUND_t* pl = (err_payload_ERR_VM_ACCESSOR_NAME_NOT_FOUND_t*)e->payload;
  size_t n = strlen(name);
  if (n >= sizeof(pl->name)) n = sizeof(pl->name) - 1;
  memcpy(pl->name, name, n);
  pl->name[n] = '\0';
  return e;
}

/*
`for_write` gates the mutability check. Reads never need it -- reading a
read-only calibration table is fine -- so it only costs anything on the write
path, and it lives here rather than in the SET entry points because this is
the only place that still holds the owning object's header.
*/
static err_h resolve_d(const vm_accessor_t* acc, uint8_t depth, bool for_write, vm_resolved_t* out) {
  out->payload = (vm_payload_t){VM_OBJ_NONE, NULL, 0};
  out->owner = NULL;

  if (unlikely(depth >= VM_ACCESSOR_MAX_DEPTH)) {
    SE_RET_ERR(ERR_VM_ACCESSOR_DEPTH_EXCEEDED, .id = acc->id);
  }

  vm_obj_h obj = vm_obj_by_id(acc->id);
  if (unlikely(!obj)) {
    SE_RET_ERR(ERR_VM_ACCESSOR_UNKNOWN_ID, .id = acc->id);
  }

  vm_payload_t p = vm_obj_as_payload(obj);
  for (uint8_t i = 0; i < acc->count; i++) {
    const vm_index_t* idx = &acc->indices[i];
    uint32_t index;
    switch (idx->kind) {
      case VM_IDX_REF: {
        /* A selector is almost always a plain object -- object(9), or one
           element of a step table -- so try to resolve it without recursing.
           The recursive call is the expensive part of a by-ref chain, not the
           read: it re-enters a function with a 160-byte frame and rotates the
           register window, twice per access for a grid[rsel][csel]. Only a
           selector that is itself chained or by-ref falls through to it. */
        /* The depth test comes first because the fast path does not count:
           it resolves a level without a frame, so letting it take the last
           one would make a chain of MAX_DEPTH+1 fit in MAX_DEPTH frames and
           pass. Declining it at the limit hands the level to resolve_d, which
           refuses it and builds the same trace it always did. */
        vm_resolved_t sub;
        if (likely(depth + 1 < VM_ACCESSOR_MAX_DEPTH && vm_resolve_fast(idx->ref, false, &sub))) {
          index = payload_as_index(sub.payload);
          break;
        }
        err_h e = resolve_d(idx->ref, depth + 1, false, &sub);
        if (unlikely(e)) {
          return SE_WRAP_ERR(e, ERR_VM_ACCESSOR_INDEX_FAILED, .id = acc->id, .chain_pos = i);
        }
        index = payload_as_index(sub.payload);
        break;
      }
      case VM_IDX_NAME: {
        /* Checked against `obj` rather than `p`: past the first step `p` still
           describes the *previous* element, while `obj` is the object about to
           be indexed -- and only a PTR array has tagged children to match. */
        if (unlikely((vm_obj_t_e)obj->head.d.obj_t != VM_OBJ_PTR)) {
          SE_RET_ERR(ERR_VM_ACCESSOR_TYPE_MISMATCH, .id = acc->id, .chain_pos = i, .expected = VM_OBJ_PTR, .actual = obj->head.d.obj_t, .obj = (void*)obj);
        }
        int32_t found = find_child_by_name(obj, idx->name, idx->name_len);
        if (unlikely(found < 0)) {
          // routine for message data whose shape varies -- not necessarily a bug
          return name_not_found_err(acc->id, i, idx->name);
        }
        index = (uint32_t)found;
        break;
      }
      case VM_IDX_LITERAL:
      default:
        index = idx->value;
        break;
    }

    p = obj_elem(obj, index);
    if (unlikely(!p.ptr)) {
      // the payload field is 16-bit; saturate so a huge index still reads as
      // "past the end" in the trace rather than as a small wrapped number
      SE_RET_ERR(ERR_VM_ACCESSOR_OOB, .id = acc->id, .chain_pos = i, .index = (uint16_t)(index > UINT16_MAX ? UINT16_MAX : index), .obj = (void*)obj);
    }

    if (i + 1 < acc->count) {
      if (unlikely(p.type != VM_OBJ_PTR)) {
        SE_RET_ERR(ERR_VM_ACCESSOR_TYPE_MISMATCH, .id = acc->id, .chain_pos = i, .expected = VM_OBJ_PTR, .actual = p.type, .obj = (void*)obj);
      }
      vm_obj_h child = *(vm_obj_h*)p.ptr;
      if (unlikely(!child)) {
        // a packet-sourced tree isn't trusted to have every PTR slot wired --
        // the untrusted-input counterpart to TYPE_MISMATCH above
        SE_RET_ERR(ERR_VM_ACCESSOR_NULL_OBJ, .id = acc->id, .chain_pos = i, .parent_obj = (void*)obj);
      }
      obj = child;
    }
  }

  if (unlikely(for_write && !obj->head.f.mutable)) {
    SE_RET_ERR(ERR_VM_ACCESSOR_NOT_MUTABLE, .id = acc->id, .chain_pos = acc->count, .obj = (void*)obj);
  }

  out->payload = p;
  out->owner = obj;
  return NULL;
}

/* Shared tail of every scalar write: convert into whatever is actually
   stored, then mark the object updated -- see vm_store_inline() in the header,
   which every write path shares. Only its cold arm lives here, where there is
   an ambient OWNER to build the error with. */
err_h vm_obj_not_scalar_err(vm_obj_h owner, vm_obj_t_e actual, uint16_t id) {
  SE_RET_ERR(ERR_VM_ACCESSOR_TYPE_MISMATCH, .id = id, .chain_pos = 0, .expected = VM_OBJ_NONE, .actual = actual, .obj = (void*)owner);
}

err_h vm_obj_get_payload(vm_payload_t* target, const vm_accessor_t* source) {
  vm_resolved_t r;
  if (likely(vm_resolve_fast(source, false, &r))) {
    *target = r.payload;
    return NULL;
  }
  SE_RET_IF_ERR(resolve_d(source, 0, false, &r));
  *target = r.payload;
  return NULL;
}

err_h vm_get_obj(vm_obj_h* target, const vm_accessor_t* source) {
  vm_resolved_t r;
  if (unlikely(!vm_resolve_fast(source, false, &r))) {
    SE_RET_IF_ERR(resolve_d(source, 0, false, &r));
  }

  /* A chain landing on a pointer element means "the object behind this slot"
     -- that is the whole point of a switch/demux cell, so follow it. Anything
     else resolves to the object the value itself lives in.

     `count > 0` matters: a chainless accessor names the object itself, so a
     PTR container must come back as the container. Following its first
     pointer would hand a relay or encoder block child[0] instead of the
     object it was wired to, with nothing to indicate the substitution. */
  if (source->count > 0 && r.payload.type == VM_OBJ_PTR && r.payload.ptr && r.payload.count >= 1) {
    vm_obj_h linked = *(vm_obj_h*)r.payload.ptr;
    if (!linked) {
      SE_RET_ERR(ERR_VM_ACCESSOR_NULL_OBJ, .id = source->id, .chain_pos = source->count, .parent_obj = (void*)r.owner);
    }
    *target = linked;
    return NULL;
  }

  *target = r.owner;
  return NULL;
}

err_h vm_obj_set_scalar(const vm_accessor_t* target, vm_val_t v, vm_obj_t_e src_type) {
  vm_resolved_t r;
  if (likely(vm_resolve_fast(target, true, &r))) {
    return vm_store_inline(r.owner, r.payload, v, src_type, target->id);
  }
  SE_RET_IF_ERR(resolve_d(target, 0, true, &r));
  return vm_store_inline(r.owner, r.payload, v, src_type, target->id);
}

/* Cold arms of the direct write path -- see vm_obj_set_scalar_direct() in the
   header, which is inline because it is how every block publishes a result. */
err_h vm_obj_null_obj_err(void) {
  SE_RET_ERR(ERR_NULL_PTR, 0);
}

err_h vm_obj_not_mutable_err(vm_obj_h obj) {
  SE_RET_ERR(ERR_VM_ACCESSOR_NOT_MUTABLE, .id = 0, .chain_pos = 0, .obj = (void*)obj);
}

err_h vm_obj_oob_err(vm_obj_h obj, uint16_t index) {
  SE_RET_ERR(ERR_VM_ACCESSOR_OOB, .id = 0, .chain_pos = 0, .index = index, .obj = (void*)obj);
}

err_h vm_obj_link_direct(vm_obj_h cell, uint16_t index, vm_obj_h child) {
  SE_CHECK_NOT_NULL(cell);
  SE_CHECK_NOT_NULL(child);
  if (!cell->head.f.mutable) {
    SE_RET_ERR(ERR_VM_ACCESSOR_NOT_MUTABLE, .id = 0, .chain_pos = 0, .obj = (void*)cell);
  }
  vm_payload_t slot = obj_elem(cell, index);
  if (!slot.ptr) {
    SE_RET_ERR(ERR_VM_ACCESSOR_OOB, .id = 0, .chain_pos = 0, .index = index, .obj = (void*)cell);
  }
  if (slot.type != VM_OBJ_PTR) {
    SE_RET_ERR(ERR_VM_ACCESSOR_TYPE_MISMATCH, .id = 0, .chain_pos = 0, .expected = VM_OBJ_PTR, .actual = slot.type, .obj = (void*)cell);
  }
  *(vm_obj_h*)slot.ptr = child;
  cell->head.f.upd = 1;
  return NULL;
}

err_h vm_obj_copy_content(const vm_accessor_t* source, const vm_accessor_t* target) {
  vm_resolved_t src, dst;
  SE_RET_IF_ERR(resolve_d(source, 0, false, &src));
  SE_RET_IF_ERR(resolve_d(target, 0, true, &dst));

  /* Pointer payloads are addresses into this program's arena -- copying the
     bytes would alias two graphs onto one child. vm_obj_link() is the
     supported way to make one object reference another. */
  if (src.payload.type == VM_OBJ_PTR || dst.payload.type == VM_OBJ_PTR) {
    SE_RET_ERR(ERR_VM_OBJ_COPY_MISMATCH, .src_type = src.payload.type, .dst_type = dst.payload.type, .src_size = src.payload.count, .dst_size = dst.payload.count);
  }

  uint8_t w = vm_type_width(src.payload.type);
  if (src.payload.type != dst.payload.type || src.payload.count != dst.payload.count || w == 0) {
    SE_RET_ERR(ERR_VM_OBJ_COPY_MISMATCH, .src_type = src.payload.type, .dst_type = dst.payload.type, .src_size = src.payload.count, .dst_size = dst.payload.count);
  }

  memcpy(dst.payload.ptr, src.payload.ptr, (size_t)w * src.payload.count);
  dst.owner->head.f.upd = 1;
  return NULL;
}

err_h vm_obj_link(const vm_accessor_t* to_join, const vm_accessor_t* owner) {
  vm_obj_h child;
  SE_RET_IF_ERR(vm_get_obj(&child, to_join));

  vm_resolved_t slot;
  SE_RET_IF_ERR(resolve_d(owner, 0, true, &slot));

  if (slot.payload.type != VM_OBJ_PTR || !slot.payload.ptr) {
    SE_RET_ERR(ERR_VM_ACCESSOR_TYPE_MISMATCH, .id = owner->id, .chain_pos = owner->count, .expected = VM_OBJ_PTR, .actual = slot.payload.type, .obj = (void*)slot.owner);
  }

  *(vm_obj_h*)slot.payload.ptr = child;
  slot.owner->head.f.upd = 1;
  return NULL;
}
