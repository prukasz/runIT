#include "vm_block_helpers.h"
#include "vm_obj_access_internal.h"

#define OWNER OWNER_VM_BLOCK

// --- User Mutation Boundaries (vm_block_obj_*) ---

err_h vm_block_obj_copy_content(const vm_accessor_t* source, const vm_accessor_t* target) {
  return vm_internal_obj_copy_content(source, target, true);
}

err_h vm_block_obj_copy_direct(vm_obj_h src, vm_obj_h dst) {
  return vm_internal_obj_copy_direct(src, dst, true);
}

err_h vm_block_obj_clone_into(const vm_accessor_t* source, const vm_accessor_t* target) {
  return vm_internal_obj_clone_into(source, target, true);
}

err_h vm_block_obj_link(const vm_accessor_t* child, const vm_accessor_t* target) {
  return vm_internal_obj_link(child, target, true);
}

err_h vm_block_obj_mark_updated(vm_obj_h obj) {
  return vm_internal_obj_mark_updated(obj, true);
}
