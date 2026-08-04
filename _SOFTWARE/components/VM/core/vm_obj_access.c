#include "vm_obj_access.h"

#define OWNER OWNER_VM_ACCESSOR

vm_obj_table_t g_vm_obj_table = {0};

err_h vm_accessor_resolve_d(const vm_obj_table_t* t, const vm_accessor_t* acc, uint8_t depth, vm_ref_t* out_ref) {
  *out_ref = (vm_ref_t){VM_OBJ_NONE, NULL, 0};

  if (depth >= VM_ACCESSOR_MAX_DEPTH) {
    SE_RET_ERR(ERR_VM_ACCESSOR_DEPTH_EXCEEDED, .id = acc->id);
  }

  vm_obj_h obj = vm_obj_get(t, acc->id);
  if (!obj) {
    SE_RET_ERR(ERR_VM_ACCESSOR_UNKNOWN_ID, .id = acc->id);
  }

  vm_ref_t ref = vm_obj_all(obj);
  for (uint8_t i = 0; i < acc->count; i++) {
    const vm_index_t* idx = &acc->indices[i];
    uint32_t index;
    if (idx->is_ref) {
      vm_ref_t idx_ref;
      err_h idx_err = vm_accessor_resolve_d(t, idx->ref, depth + 1, &idx_ref);
      if (idx_err) {
        return SE_WRAP_ERR(idx_err, ERR_VM_ACCESSOR_INDEX_FAILED, .id = acc->id, .chain_pos = i);
      }
      index = vm_ref_as_index(idx_ref);
    } else {
      index = idx->value;
    }

    ref = vm_obj_at(obj, (uint16_t)index);
    if (!ref.ptr) {
      SE_RET_ERR(ERR_VM_ACCESSOR_OOB, .id = acc->id, .chain_pos = i, .index = (uint16_t)index, .obj = (void*)obj);
    }

    if (i + 1 < acc->count) {
      if (ref.type != VM_OBJ_PTR) {
        SE_RET_ERR(ERR_VM_ACCESSOR_TYPE_MISMATCH, .id = acc->id, .chain_pos = i,
                   .expected = VM_OBJ_PTR, .actual = ref.type, .obj = (void*)obj);
      }
      vm_obj_h child = *(vm_obj_h*)ref.ptr;
      if (!child) {
        // a packet-sourced tree isn't trusted to have every PTR slot wired --
        // this is the untrusted-input counterpart to TYPE_MISMATCH above
        SE_RET_ERR(ERR_VM_ACCESSOR_NULL_OBJ, .id = acc->id, .chain_pos = i, .parent_obj = (void*)obj);
      }
      obj = child;
    }
  }
  *out_ref = ref;
  return NULL;
}

err_h vm_accessor_resolve(const vm_obj_table_t* t, const vm_accessor_t* acc, vm_ref_t* out_ref) {
  return vm_accessor_resolve_d(t, acc, 0, out_ref);
}
