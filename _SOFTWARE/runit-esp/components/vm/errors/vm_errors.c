#include "vm_errors.h"

#include "vm_obj.h"

ERR_MODULE_DEFINE(MODULE_ID_VM, ERR_VM_MAP)

// A 32-bit index in a 16-bit field: saturated, so a huge one still reads as past the end, not as a small one.
static inline uint16_t sat_u16(uint32_t v) {
  return (uint16_t)(v > UINT16_MAX ? UINT16_MAX : v);
}

/* Accessor layer */

__attribute__((noinline)) err_h vm_err_depth(uint16_t id) {
  return ERR_NEW(ERR_VM_ACC_DEPTH_EXCEEDED, .id = id);
}

__attribute__((noinline)) err_h vm_err_unknown_id(uint16_t id) {
  return ERR_NEW(ERR_VM_ACC_UNKNOWN_ID, .id = id);
}

__attribute__((noinline)) err_h vm_err_expected_ptr(uint16_t id, uint8_t pos, uint8_t actual, uint16_t obj_id) {
  return ERR_NEW(ERR_VM_ACC_TYPE_MISMATCH, .id = id, .chain_pos = pos, .expected = VM_OBJ_PTR, .actual = actual,
                 .obj_id = obj_id);
}

__attribute__((noinline)) err_h vm_err_chain_oob(uint16_t id, uint8_t pos, uint32_t index, uint16_t obj_id) {
  return ERR_NEW(ERR_VM_ACC_OOB, .id = id, .chain_pos = pos, .index = sat_u16(index), .obj_id = obj_id);
}

__attribute__((noinline)) err_h vm_err_null_obj(uint16_t id, uint8_t pos, uint16_t parent_id) {
  return ERR_NEW(ERR_VM_ACC_NULL_OBJ, .id = id, .chain_pos = pos, .parent_id = parent_id);
}

__attribute__((noinline)) err_h vm_err_index_failed(err_h cause, uint16_t id, uint8_t pos) {
  return ERR_WRAP(cause, ERR_VM_ACC_INDEX_FAILED, .id = id, .chain_pos = pos);
}

__attribute__((noinline)) err_h vm_err_name_not_found(uint16_t id, uint8_t pos) {
  return ERR_NEW(ERR_VM_ACC_NAME_NOT_FOUND, .id = id, .chain_pos = pos);
}

__attribute__((noinline)) err_h vm_obj_not_scalar_err(uint16_t owner_id, uint8_t actual, uint16_t id) {
  return ERR_NEW(ERR_VM_ACC_TYPE_MISMATCH, .id = id, .chain_pos = 0, .expected = VM_OBJ_NONE, .actual = actual,
                 .obj_id = owner_id);
}

/* Object layer */

__attribute__((noinline)) err_h vm_obj_null_obj_err(void) {
  return ERR_NEW(ERR_NULL_PTR);
}

__attribute__((noinline)) err_h vm_obj_not_mutable_err(uint16_t obj_id) {
  return ERR_NEW(ERR_VM_OBJ_NOT_MUTABLE, .obj_id = obj_id);
}

__attribute__((noinline)) err_h vm_obj_oob_err(uint16_t obj_id, uint32_t index) {
  return ERR_NEW(ERR_VM_OBJ_OOB, .index = sat_u16(index), .obj_id = obj_id);
}

__attribute__((noinline)) err_h vm_obj_not_ptr_err(uint16_t obj_id, uint8_t actual) {
  return ERR_NEW(ERR_VM_OBJ_NOT_PTR, .actual = actual, .obj_id = obj_id);
}

/* Block layer */

__attribute__((noinline)) err_h vm_block_err_pin_missing(uint16_t block_idx, uint8_t pin_id, bool is_out) {
  return ERR_NEW(ERR_VM_BLK_PIN_MISSING, .block_idx = block_idx, .pin_id = pin_id, .is_out = is_out);
}

__attribute__((noinline)) err_h vm_block_err_pin_unlinked(uint16_t block_idx, uint8_t pin_id, bool is_out) {
  return ERR_NEW(ERR_VM_BLK_PIN_UNLINKED, .block_idx = block_idx, .pin_id = pin_id, .is_out = is_out);
}
