#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_block.h"

/*
Block construction -- the counterpart to vm_block.h, which only ever reads
blocks that already exist. Same split as vm_obj_build.h / vm_obj_access.h, and
the same allocation rules: one chunk from vm_store_alloc(), never free one
block, reclaim by resetting the whole store.

A block is the last thing a program builds, because it names everything else.
Its inputs are accessor ids, its outputs and ENO are object ids, and its EN is
an accessor id -- so objects and accessors must already exist and be bound
before the block referring to them can be created. That is the same ordering
rule a VM_OBJ_PTR parent has with its children, one level up.

Ids are resolved to pointers here, once, and the block stores only the
pointers. A block therefore cannot hold an id that stops being valid, and no
execution path ever pays a table lookup.
*/

typedef struct vm_block_cfg_t {
  uint16_t block_idx;   // the program's own identity for this block
  uint8_t block_type;   // which palette entry executes this block
  uint8_t in_cnt;       // <= VM_BLOCK_MAX_IN
  uint8_t q_cnt;        // <= VM_BLOCK_MAX_OUT
  uint8_t en_cnt;       // <= VM_BLOCK_MAX_EN; 0 = root, always enabled
  uint8_t en_mode;      // VM_BLK_EN_ANY / _ALL
  uint8_t on_error;     // VM_BLK_ERR_STOP / _CONTINUE
  uint16_t custom_len;  // bytes of private state; zeroed on creation

  /* Wiring, as ids into the tables built earlier in the load. A present id
     must resolve; VM_BLOCK_NO_ID means "nothing here" for an *input* (the
     block uses its own constant instead) and for ENO (publishes nothing).
     An absent EN has no spelling of its own -- it is simply en_cnt == 0, and
     a *listed* enable source must always resolve. An output must always
     resolve too: a block with nowhere to publish is malformed. */
  const uint16_t* in_acc_ids;   // in_cnt entries
  const uint16_t* out_obj_ids;  // q_cnt entries
  const uint16_t* en_acc_ids;   // en_cnt entries
  uint16_t eno_obj_id;
} vm_block_cfg_t;

/** @brief "No id here" -- legal for EN and ENO, rejected for a numbered pin.
 *  0xFFFF cannot be a valid id because a table's count is itself uint16_t, so
 *  the highest addressable slot is 0xFFFE. */
#define VM_BLOCK_NO_ID 0xFFFFu

/**
 * @brief Bump-allocate one block and resolve all of its wiring.
 *
 * The whole block is a single slice -- `[cfg][inputs][outputs][custom_data]`
 * -- so there is one allocation and nothing to keep in sync. custom_data is
 * zeroed, so a block's private state starts clean rather than holding whatever
 * the previous program left in the arena.
 *
 * @param out Receives the new handle; set to NULL on any failure.
 * @param id Registry id to bind, or VM_ID_NONE to allocate without binding.
 * @param cfg Shape and wiring ids.
 * @return err_h NULL on success. ERR_VM_BLK_BAD_SHAPE when a count exceeds its
 *         VM_BLOCK_MAX_*, ERR_VM_BLK_BAD_REF when any id fails to resolve
 *         (carrying which slot and which of input/output/EN/ENO it was), or
 *         ERR_BASE_NO_MEM when the arena is full.
 *
 * @code
 * uint16_t ins[2] = {4, 5};
 * uint16_t outs[1] = {9};
 * vm_block_h b;
 * SE_RET_IF_ERR(vm_block_create(&b, 0, &(vm_block_cfg_t){
 *     .block_idx = 1, .block_type = BLK_ADD, .in_cnt = 2, .q_cnt = 1,
 *     .in_acc_ids = ins, .out_obj_ids = outs,
 *     .en_cnt = 0, .eno_obj_id = VM_BLOCK_NO_ID}));
 * @endcode
 */
err_h vm_block_create(vm_block_h* out, uint16_t id, const vm_block_cfg_t* cfg);

