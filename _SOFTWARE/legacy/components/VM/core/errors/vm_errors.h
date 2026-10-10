#pragma once
#include "sys_error.h"
#include "vm_obj.h"

/* Cold-path error builders for accessor, object, and block layers.
   Extracted as noinline to prevent stack frame inflation on hot paths.

   They take object IDs, not handles: this module knows nothing of the object layer (it only needs
   the vm_obj.h types), and the callers -- which hold the handles -- resolve the ID with
   vm_obj_get_id() on their failure path. That keeps the dependency one way: obj -> errors. */

/* ========================================================================= */
/* Accessor Layer Errors                                                     */
/* ========================================================================= */

SE_MUST_USE err_h vm_err_depth(uint16_t id);
SE_MUST_USE err_h vm_err_unknown_id(uint16_t id);
SE_MUST_USE err_h vm_err_expected_ptr(uint16_t id, uint8_t pos, uint8_t actual, uint16_t obj_id);
SE_MUST_USE err_h vm_err_chain_oob(uint16_t id, uint8_t pos, uint32_t index, uint16_t obj_id);
SE_MUST_USE err_h vm_err_null_obj(uint16_t id, uint8_t pos, uint16_t parent_id);
SE_MUST_USE err_h vm_err_chain_not_mutable(uint16_t id, uint8_t pos, uint16_t obj_id);
SE_MUST_USE err_h vm_err_index_failed(err_h cause, uint16_t id, uint8_t pos);
SE_MUST_USE err_h vm_err_name_not_found(uint16_t id, uint8_t pos, const char* name, size_t name_len);
SE_MUST_USE err_h vm_obj_not_scalar_err(uint16_t owner_id, vm_obj_t_e actual, uint16_t id);

/* ========================================================================= */
/* Object Layer Errors                                                       */
/* ========================================================================= */

SE_MUST_USE err_h vm_obj_null_obj_err(void);
SE_MUST_USE err_h vm_obj_not_mutable_err(uint16_t obj_id);
SE_MUST_USE err_h vm_obj_oob_err(uint16_t obj_id, uint32_t index);
SE_MUST_USE err_h vm_obj_not_ptr_err(uint16_t obj_id, uint8_t actual);

/* ========================================================================= */
/* Block Layer Errors                                                        */
/* ========================================================================= */

SE_MUST_USE err_h vm_block_err_pin_missing(uint16_t block_idx, uint8_t pin_id, bool is_out);
SE_MUST_USE err_h vm_block_err_pin_unlinked(uint16_t block_idx, uint8_t pin_id, bool is_out);
