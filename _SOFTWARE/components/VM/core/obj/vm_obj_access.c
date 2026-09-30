#include "vm_obj_access_internal.h"

#define OWNER OWNER_VM_ACCESSOR

/*
 * Object Access & Resolution Public APIs
 *
 * Logic Flow:
 *   1. Read APIs (vm_obj_get_payload, vm_obj_get_obj, vm_obj_get_owner, vm_obj_get_child)
 *   2. Write APIs (vm_obj_clear_quiet)
 *   3. Copy & Clone APIs (vm_obj_copy_content, vm_obj_shape_matches, vm_obj_schema_matches, vm_obj_clone_shape, vm_obj_clone_into)
 *   4. Link APIs (vm_obj_link, vm_obj_link_direct)
 *   5. Update / Freshness Mark APIs (vm_obj_mark_updated)
 */

// ===========================================================================
// 1. Read APIs
// ===========================================================================

err_h vm_obj_get_payload(vm_obj_payload_t* target, const vm_accessor_t* source) {
  SE_CHECK_NOT_NULL(target);
  *target = (vm_obj_payload_t){0};  // deterministic output on resolution failure
  return vm_internal_obj_resolve(source, target);
}

err_h vm_obj_get_obj(vm_obj_h* target, const vm_accessor_t* source) {
  SE_CHECK_NOT_NULL(target);
  *target = NULL;
  vm_obj_payload_t p;
  SE_TRY(vm_internal_obj_resolve(source, &p));

  // A chain landing on a pointer element means "the object behind this
  // slot" (the point of a switch/demux cell), so follow it. `count > 0`
  // matters: a chainless accessor names the object itself, so a PTR
  // container must come back as-is rather than silently substituting child[0].
  if (source->count > 0 && p.type == VM_OBJ_PTR && p.ptr && p.count >= 1) {
    vm_obj_h linked = *(vm_obj_h*)p.ptr;
    if (!linked) {
      SE_FAIL(ERR_VM_ACCESSOR_NULL_OBJ, .id = source->id, .chain_pos = source->count, .parent_id = vm_obj_get_id(p.owner));
    }
    *target = linked;
    return NULL;
  }

  *target = p.owner;
  return NULL;
}

err_h vm_obj_get_owner(vm_obj_h* target, const vm_accessor_t* source) {
  SE_CHECK_NOT_NULL(target);
  *target = NULL;
  vm_obj_payload_t p;
  SE_TRY(vm_internal_obj_resolve(source, &p));
  *target = p.owner;
  return NULL;
}

err_h vm_obj_get_child(vm_obj_h* target, const vm_accessor_t* parent, const char* name, size_t name_len) {
  SE_CHECK_NOT_NULL(target);
  SE_CHECK_NOT_NULL(name);
  vm_obj_h obj;
  SE_TRY(vm_obj_get_obj(&obj, parent));
  if (obj->head.d.obj_t != VM_OBJ_PTR) return vm_err_expected_ptr(parent->id, parent->count, obj->head.d.obj_t, obj);
  int32_t index = name_len <= VM_OBJ_NAME_MAX ? find_child_by_name(obj, name, (uint8_t)name_len) : -1;
  if (index < 0) {
    char   tag[VM_OBJ_NAME_MAX + 1];
    size_t n = name_len < VM_OBJ_NAME_MAX ? name_len : VM_OBJ_NAME_MAX;
    memcpy(tag, name, n);
    tag[n] = '\0';
    return vm_err_name_not_found(parent->id, parent->count, tag, n);
  }
  *target = ((vm_obj_h*)obj->payload)[index];
  return NULL;
}

// ===========================================================================
// 2. Write APIs
// ===========================================================================


err_h vm_obj_clear_quiet(vm_obj_h obj) {
  SE_TRY(vm_internal_obj_writable(obj, false));
  if (!vm_type_is_scalar(obj->head.d.obj_t)) return vm_obj_not_scalar_err(obj, (vm_obj_t_e)obj->head.d.obj_t, VM_ID_NONE);
  if (obj->head.payload_size) memset(obj->payload, 0, obj->head.payload_size);
  return NULL;
}

// ===========================================================================
// 3. Copy & Clone APIs
// ===========================================================================

err_h vm_obj_copy_content(const vm_accessor_t* source, const vm_accessor_t* target) {
  return vm_internal_obj_copy_content(source, target, false);
}

err_h vm_obj_copy_direct(vm_obj_h src, vm_obj_h dst) {
  return vm_internal_obj_copy_direct(src, dst, false);
}

bool vm_obj_shape_matches(vm_obj_h a, vm_obj_h b) {
  return vm_internal_shape_matches(a, b, 0, false);
}

bool vm_obj_schema_matches(vm_obj_h a, vm_obj_h b) {
  return vm_internal_shape_matches(a, b, 0, true);
}

err_h vm_obj_clone_shape(vm_obj_h* out, vm_obj_h src) {
  SE_CHECK_NOT_NULL(out);
  SE_CHECK_NOT_NULL(src);
  return vm_internal_clone_shape(out, src, 0);
}

err_h vm_obj_clone_into(const vm_accessor_t* source, const vm_accessor_t* target) {
  return vm_internal_obj_clone_into(source, target, false);
}

// ===========================================================================
// 4. Link APIs
// ===========================================================================

err_h vm_obj_link(const vm_accessor_t* child, const vm_accessor_t* target) {
  return vm_internal_obj_link(child, target, false);
}

err_h vm_obj_link_direct(vm_obj_h cell, uint16_t index, vm_obj_h child) {
  SE_CHECK_NOT_NULL(cell);
  SE_CHECK_NOT_NULL(child);
  if (!cell->head.f.mutable) return vm_obj_not_mutable_err(cell);
  vm_obj_payload_t slot = obj_elem(cell, index);
  if (!slot.ptr) return vm_obj_oob_err(cell, index);
  if (slot.type != VM_OBJ_PTR) return vm_obj_not_ptr_err(cell, slot.type);
  return slot_store(cell, (vm_obj_h*)slot.ptr, child);
}

// ===========================================================================
// 5. Update / Freshness Mark APIs
// ===========================================================================

err_h vm_obj_mark_updated(vm_obj_h obj) {
  return vm_internal_obj_mark_updated(obj, false);
}

uint16_t vm_obj_get_id(vm_obj_h obj) {
  if (!obj) return VM_ID_NONE;
  if (vm_obj_is_dynamic(obj)) {
    uint16_t did = vm_obj_dyn_get_id(obj);
    return (did != VM_DYN_NO_ID) ? (did | VM_OBJ_ID_DYN_BIT) : VM_ID_NONE;
  }
  const vm_registry_t* g = &g_vm_store.reg[VM_REG_OBJ];
  if (g && g->items) {
    for (uint16_t i = 0; i < g->count; ++i) {
      if (g->items[i] == (void*)obj) return i;
    }
  }
  return VM_ID_NONE;
}
