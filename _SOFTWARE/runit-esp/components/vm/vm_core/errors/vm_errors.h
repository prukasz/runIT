#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"
#include "vm_id.h"

/* Errors of the VM (module MODULE_ID_VM): the core's and the blocks'. A bad argument, no memory, a value out of range
 * and a full table are the core tags (ERR_BAD_ARG, ERR_NULL_PTR, ERR_NO_MEM, ERR_RANGE, ERR_FULL), not the VM's.
 * `id` / `obj_id` / `parent_id` carry the failing object's ID (VM_OBJ_ID_DYN_BIT | slot for a heap object, VM_ID_NONE
 * for none); `block_idx` / `blk_id` a block's, `id` / `acc_id` of an accessor error the accessor's. */

#define ERR_VM_MAP(X, M)                                                                                                                                                                                \
  X(M, ERR_VM_ALLOC_EXHAUSTED,          1,  ERR_SEV_HIGH,     { uint32_t requested; uint32_t remaining; }) /* the program's arena is used up while loading */                                           \
  X(M, ERR_VM_ACC_UNKNOWN_ID,           2,  ERR_SEV_LOW,      { uint16_t id; })                                                                                                                         \
  X(M, ERR_VM_ACC_OOB,                  3,  ERR_SEV_LOW,      { uint16_t id; uint8_t chain_pos; uint16_t index; uint16_t obj_id; }) /* a position outside the object */                                 \
  X(M, ERR_VM_ACC_TYPE_MISMATCH,        4,  ERR_SEV_LOW,      { uint16_t id; uint8_t chain_pos; uint8_t expected; /*@enum-ref vm_obj_t_e*/ uint8_t actual; /*@enum-ref vm_obj_t_e*/ uint16_t obj_id; }) \
  X(M, ERR_VM_ACC_NULL_OBJ,             5,  ERR_SEV_LOW,      { uint16_t id; uint8_t chain_pos; uint16_t parent_id; })                                                                                  \
  X(M, ERR_VM_ACC_DEPTH_EXCEEDED,       6,  ERR_SEV_LOW,      { uint16_t id; })                                                                                                                         \
  X(M, ERR_VM_ACC_INDEX_FAILED,         7,  ERR_SEV_LOW,      { uint16_t id; uint8_t chain_pos; }) /* context: the step whose index read failed */                                                      \
  X(M, ERR_VM_ACC_NAME_NOT_FOUND,       8,  ERR_SEV_LOW,      { uint16_t id; uint8_t chain_pos; }) /* the app names the step's name */                                                                  \
  X(M, ERR_VM_ACC_STEP_OOB,             9,  ERR_SEV_LOW,      { uint16_t acc_id; uint8_t pos; uint8_t count; }) /* load: a step outside the accessor's steps */                                         \
  X(M, ERR_VM_ACC_BAD_KIND,             10, ERR_SEV_LOW,      { uint16_t acc_id; uint8_t pos; uint8_t kind; /*@enum-ref vm_index_kind_e*/ })                                                            \
  X(M, ERR_VM_BLK_PIN_MISSING,          11, ERR_SEV_LOW,      { uint16_t block_idx; uint8_t pin_id; uint8_t is_out; })                                                                                  \
  X(M, ERR_VM_BLK_PIN_UNLINKED,         12, ERR_SEV_LOW,      { uint16_t block_idx; uint8_t pin_id; uint8_t is_out; })                                                                                  \
  X(M, ERR_VM_BLK_FAILED,               13, ERR_SEV_MEDIUM,   { uint16_t block_idx; uint8_t block_type; /*@id vm-block-type*/ }) /* context: the block of a report */                                   \
  X(M, ERR_VM_BLK_BAD_SHAPE,            14, ERR_SEV_LOW,      { uint16_t blk_id; uint8_t in_cnt; uint8_t q_cnt; })                                                                                      \
  X(M, ERR_VM_BLK_BAD_REF,              15, ERR_SEV_LOW,      { uint16_t blk_id; uint16_t ref_id; uint8_t slot; uint8_t kind; }) /* kind: vm_ref_kind */                                                \
  X(M, ERR_VM_BLK_UNKNOWN_TYPE,         16, ERR_SEV_LOW,      { uint16_t blk_id; uint8_t block_type; /*@id vm-block-type*/ })                                                                           \
  X(M, ERR_VM_BLK_OUTPUT_TAKEN,         17, ERR_SEV_LOW,      { uint16_t blk_id; uint16_t obj_id; uint8_t slot; uint8_t kind; }) /* kind: vm_ref_kind */                                                \
  X(M, ERR_VM_OBJ_COPY_MISMATCH,        18, ERR_SEV_LOW,      { uint8_t src_type; /*@enum-ref vm_obj_t_e*/ uint8_t dst_type; /*@enum-ref vm_obj_t_e*/ uint16_t src_size; uint16_t dst_size; })          \
  X(M, ERR_VM_OBJ_COPY_SHAPE,           19, ERR_SEV_LOW,      { uint16_t index; uint8_t depth; uint8_t reason; }) /* reason: VM_COPY_SHAPE_* */                                                         \
  X(M, ERR_VM_OBJ_NOT_MUTABLE,          20, ERR_SEV_LOW,      { uint16_t obj_id; })                                                                                                                     \
  X(M, ERR_VM_OBJ_OOB,                  21, ERR_SEV_LOW,      { uint16_t index; uint16_t obj_id; })                                                                                                     \
  X(M, ERR_VM_OBJ_NOT_PTR,              22, ERR_SEV_LOW,      { uint8_t actual; /*@enum-ref vm_obj_t_e*/ uint16_t obj_id; })                                                                            \
  X(M, ERR_VM_OBJ_BAD_TYPE,             23, ERR_SEV_LOW,      { uint8_t type; /*@enum-ref vm_obj_t_e*/ })                                                                                               \
  X(M, ERR_VM_OBJ_EMPTY,                24, ERR_SEV_LOW,      { uint8_t type; /*@enum-ref vm_obj_t_e*/ })                                                                                               \
  X(M, ERR_VM_OBJ_BAD_SIZE,             25, ERR_SEV_LOW,      { uint8_t type; /*@enum-ref vm_obj_t_e*/ uint16_t payload_size; uint8_t width; })                                                         \
  X(M, ERR_VM_OBJ_NAME_TOO_LONG,        26, ERR_SEV_LOW,      { uint8_t len; })                                                                                                                         \
  X(M, ERR_VM_OBJ_RETENTIVE_PTR,        27, ERR_SEV_LOW,      { uint8_t type; /*@enum-ref vm_obj_t_e*/ })                                                                                               \
  X(M, ERR_VM_OBJ_OWNERSHIP,            28, ERR_SEV_LOW,      { uint8_t reason; uint8_t limit; })                                                                                                       \
  X(M, ERR_VM_OBJ_USR_PROTECTED,        29, ERR_SEV_LOW,      { uint16_t obj_id; })                                                                                                                     \
  X(M, ERR_VM_LOAD_BAD_STATE,           30, ERR_SEV_LOW,      { uint8_t state; uint8_t expected; })                                                                                                     \
  X(M, ERR_VM_LOAD_TOO_BIG,             31, ERR_SEV_LOW,      { uint32_t requested; uint32_t available; }) /* OPEN: more than the pool */                                                               \
  X(M, ERR_VM_LOAD_DATA_RANGE,          32, ERR_SEV_LOW,      { uint16_t id; uint16_t start_idx; uint16_t len; uint16_t items; })                                                                       \
  X(M, ERR_VM_LOAD_SHORT_RECORD,        33, ERR_SEV_LOW,      { uint8_t packet; uint16_t need; uint16_t got; })                                                                                         \
  X(M, ERR_VM_LOAD_RUNNING,             34, ERR_SEV_LOW,      { uint8_t mode; })                                                                                                                        \
  X(M, ERR_VM_LOAD_EMPTY,               35, ERR_SEV_LOW,      {})                                                                                                                                       \
  X(M, ERR_VM_LOAD_ABORTED,             36, ERR_SEV_MEDIUM,   { uint8_t packet; }) /* context: the failed load op (fail closed) */                                                                      \
  X(M, ERR_VM_LOAD_INCOMPLETE,          37, ERR_SEV_LOW,      { uint8_t kind; uint16_t id; })                                                                                                           \
  X(M, ERR_VM_REG_OOB,                  38, ERR_SEV_LOW,      { uint8_t kind; uint16_t id; uint16_t count; })                                                                                           \
  X(M, ERR_VM_REG_DUP,                  39, ERR_SEV_LOW,      { uint8_t kind; uint16_t id; })                                                                                                           \
  X(M, ERR_VM_EXEC_BLOCK_HUNG,          40, ERR_SEV_HIGH,     { uint16_t block_idx; uint16_t ms; })                                                                                                     \
  X(M, ERR_VM_EXEC_SPAN_DEPTH,          41, ERR_SEV_LOW,      { uint16_t block_idx; uint8_t depth; })                                                                                                   \
  X(M, ERR_VM_EXEC_BAD_SPAN,            42, ERR_SEV_LOW,      { uint16_t block_idx; uint16_t start; uint16_t end; })                                                                                    \
  X(M, ERR_VM_EXEC_CONTROL,             43, ERR_SEV_LOW,      { uint8_t command; /*@enum-ref vm_exec_command_e*/ uint8_t mode; })                                                                       \
  X(M, ERR_VM_EXEC_SELF_BARRIER,        44, ERR_SEV_LOW,      { uint8_t operation; })                                                                                                                   \
  X(M, ERR_VM_EXEC_FAULT_LATCHED,       45, ERR_SEV_CRITICAL, { uint16_t device_id; /*@id device*/ uint32_t root_tag; /*@id error-tag*/ })                                                              \
  X(M, ERR_VM_EVENT_OVERFLOW,           46, ERR_SEV_MEDIUM,   { uint16_t source; uint16_t depth; uint16_t dropped; })                                                                                   \
  X(M, ERR_VM_EXPR_BAD_CODE,            47, ERR_SEV_LOW,      { uint16_t block_idx; uint16_t pc; uint8_t opcode; /*@enum-ref vm_expr_op_e*/ uint8_t reason; })                                          \
  X(M, ERR_VM_EXPR_MATH,                48, ERR_SEV_LOW,      { uint16_t block_idx; uint16_t pc; uint8_t opcode; /*@enum-ref vm_expr_op_e*/ uint8_t reason; })                                          \
  X(M, ERR_VM_FOR_BAD_LOOP,             49, ERR_SEV_LOW,      { uint16_t block_idx; uint32_t turns; uint16_t cap; uint8_t reason; })                                                                    \
  X(M, ERR_VM_PERIODIC_OVERRUN,         50, ERR_SEV_LOW,      { uint16_t block_idx; uint32_t missed; uint32_t period_ms; })                                                                             \
  X(M, ERR_VM_DEV_HELD,                 51, ERR_SEV_MEDIUM,   { uint16_t block_idx; uint8_t field; float value; float min; float max; }) /* a device block's value held to its limit */                 \
  X(M, ERR_VM_OVERRIDE_PTR_UNSUPPORTED, 52, ERR_SEV_LOW,      { uint16_t obj_id; })                                                                                                                     \
  X(M, ERR_VM_OVERRIDE_BAD_RECORD,      53, ERR_SEV_LOW,      { uint16_t obj_id; uint16_t declared_len; uint16_t item_size; })                                                                          \
  X(M, ERR_VM_RETAIN_UNNAMED,           54, ERR_SEV_LOW,      { uint16_t obj_id; })                                                                                                                     \
  X(M, ERR_VM_RETAIN_TOO_BIG,           55, ERR_SEV_LOW,      { uint16_t obj_id; uint32_t need; uint32_t max; })                                                                                        \
  X(M, ERR_VM_RETAIN_MISMATCH,          56, ERR_SEV_LOW,      { uint16_t obj_id; uint8_t type; /*@enum-ref vm_obj_t_e*/ uint16_t size; })                                                               \
  X(M, ERR_VM_RETAIN_CORRUPT,           57, ERR_SEV_MEDIUM,   { uint32_t offset; })

ERR_TAGS_DECLARE(MODULE_ID_VM, ERR_VM_MAP)

/* Cold-path builders of the accessor, object and block layers (vm_errors.c): noinline, so the hot paths that fail
 * into them keep a small stack frame. They take IDs, not handles: the callers resolve the ID (vm_obj_get_id) on
 * their failure path, so this header needs no layer of the VM. */

/* Accessor layer */
MUST_USE err_h vm_err_depth(uint16_t id);
MUST_USE err_h vm_err_unknown_id(uint16_t id);
MUST_USE err_h vm_err_expected_ptr(uint16_t id, uint8_t pos, uint8_t actual, uint16_t obj_id);
MUST_USE err_h vm_err_chain_oob(uint16_t id, uint8_t pos, uint32_t index, uint16_t obj_id);
MUST_USE err_h vm_err_null_obj(uint16_t id, uint8_t pos, uint16_t parent_id);
MUST_USE err_h vm_err_index_failed(err_h cause, uint16_t id, uint8_t pos);
MUST_USE err_h vm_err_name_not_found(uint16_t id, uint8_t pos);
MUST_USE err_h vm_obj_not_scalar_err(uint16_t owner_id, uint8_t actual, uint16_t id);

/* Object layer */
MUST_USE err_h vm_obj_null_obj_err(void);
MUST_USE err_h vm_obj_not_mutable_err(uint16_t obj_id);
MUST_USE err_h vm_obj_oob_err(uint16_t obj_id, uint32_t index);
MUST_USE err_h vm_obj_not_ptr_err(uint16_t obj_id, uint8_t actual);

/* Block layer */
MUST_USE err_h vm_block_err_pin_missing(uint16_t block_idx, uint8_t pin_id, bool is_out);
MUST_USE err_h vm_block_err_pin_unlinked(uint16_t block_idx, uint8_t pin_id, bool is_out);
