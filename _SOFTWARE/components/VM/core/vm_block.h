#pragma once
#include <stddef.h>
#include <stdint.h>
#include "vm_obj_access.h"

/*
One block = one linear-allocator blob, sized once at creation:
  [cfg][in_cnt * const vm_accessor_t*][q_cnt * vm_obj_h][custom_data bytes]

No separate allocations for inputs/outputs/custom_data, and nothing to keep
in sync -- their positions are derived from in_cnt/q_cnt, not stored.

Inputs and outputs are deliberately different shapes. An input can be wired
to anything -- any object, any element, possibly through a switch cell, with
a possibly dynamic index -- so it needs the full accessor. An output is bound
once at load time to an object this block owns, and never moves, so there is
nothing to resolve: the slot holds the vm_obj_h itself. That is both smaller
(no chain descriptor) and free at runtime (no walk at all).
*/

/* EN/ENO sit outside the numbered pins so the supervisor can gate every block
   identically without knowing which pin index a given block type happens to
   use. They are ordinary boolean objects otherwise -- to gate on several
   conditions, wire them through an Expression block and feed its single
   result to EN, rather than expecting a subscription list here. */

typedef struct vm_block_data_t {
  struct {
    uint16_t block_idx;
    uint16_t node_idx;
    uint16_t connected_in;  // bitmask of used inputs (for hardoced inputs - blocks)
    uint8_t block_type;
    uint8_t in_cnt;
    uint8_t q_cnt;
    uint8_t status;
    const vm_accessor_t* en;  // NULL = always enabled (the IEC 61131-3 default)
    vm_obj_h eno;             // NULL = block publishes no ENO
  } cfg;
  uint8_t data[];
} vm_block_data_t;

typedef vm_block_data_t* vm_block_h;

static inline const vm_accessor_t** vm_block_inputs(vm_block_h b) {
  return (const vm_accessor_t**)b->data;
}

static inline vm_obj_h* vm_block_outputs(vm_block_h b) {
  return (vm_obj_h*)(vm_block_inputs(b) + b->cfg.in_cnt);
}

static inline void* vm_block_custom_data(vm_block_h b) {
  return (void*)(vm_block_outputs(b) + b->cfg.q_cnt);
}

// total bytes required for one block of this shape -- what the loader uses
// to size a single linear-allocator slot before writing into it
static inline size_t vm_block_size(uint8_t in_cnt, uint8_t q_cnt, uint16_t custom_len) {
  return sizeof(vm_block_data_t) + (size_t)in_cnt * sizeof(const vm_accessor_t*) + (size_t)q_cnt * sizeof(vm_obj_h) + custom_len;
}

/**
 * @brief Fetch input pin `id`'s accessor.
 * @return err_h NULL on success, ERR_VM_BLOCK_PIN_MISSING if the block has no
 *         such pin, ERR_VM_BLOCK_PIN_UNLINKED if the pin exists but nothing
 *         is wired to it.
 */
static inline err_h vm_block_get_in(const vm_accessor_t** target, vm_block_h b, uint8_t id) {
  if (id >= b->cfg.in_cnt) {
    SE_RET_ERR_OWNED(OWNER_VM_BLOCK, ERR_VM_BLOCK_PIN_MISSING, .block_idx = b->cfg.block_idx, .pin_id = id, .is_out = 0);
  }
  const vm_accessor_t* acc = vm_block_inputs(b)[id];
  if (!acc) {
    SE_RET_ERR_OWNED(OWNER_VM_BLOCK, ERR_VM_BLOCK_PIN_UNLINKED, .block_idx = b->cfg.block_idx, .pin_id = id, .is_out = 0);
  }
  *target = acc;
  return NULL;
}

/** @brief Fetch output pin `id`'s object -- already bound, nothing to resolve.
 *  Same errors as vm_block_get_in(). */
static inline err_h vm_block_get_out(vm_obj_h* target, vm_block_h b, uint8_t id) {
  if (id >= b->cfg.q_cnt) {
    SE_RET_ERR_OWNED(OWNER_VM_BLOCK, ERR_VM_BLOCK_PIN_MISSING, .block_idx = b->cfg.block_idx, .pin_id = id, .is_out = 1);
  }
  vm_obj_h obj = vm_block_outputs(b)[id];
  if (!obj) {
    SE_RET_ERR_OWNED(OWNER_VM_BLOCK, ERR_VM_BLOCK_PIN_UNLINKED, .block_idx = b->cfg.block_idx, .pin_id = id, .is_out = 1);
  }
  *target = obj;
  return NULL;
}

/*
The supervisor calls every block every cycle; the block itself decides what
being disabled means for it. That is deliberate -- if the supervisor simply
skipped disabled blocks, an actuator could never define a safe fallback, and
"hold whatever was last commanded" is usually the wrong failure mode:

  IF_BLOCK_ENABLED(block) {
    ... normal work ...
    vm_block_set_ENO(block, true);
  } else {
    servo_go_home(...);            // defined safe state, not "whatever was last"
    vm_block_set_ENO(block, false);
  }

An unwired EN reads as enabled, so blocks that never gate cost one NULL test.
A failed EN resolve reads as *disabled* -- if the gate itself cannot be
evaluated, running the body is the more dangerous guess.
*/
static inline bool vm_block_is_enabled(vm_block_h b) {
  if (!b->cfg.en) return true;
  bool en = false;
  err_h e = VM_OBJ_GET_VAL(en, b->cfg.en);
  if (e) {
    SE_push_to_handler(SE_WRAP_ERR_OWNED(OWNER_VM_BLOCK, e, ERR_VM_BLOCK_FAILED, .block_idx = b->cfg.block_idx, .block_type = b->cfg.block_type));
    return false;
  }
  return en;
}

#define IF_BLOCK_ENABLED(block) if (vm_block_is_enabled(block))

/** @brief Publish this block's ENO. No-op when the block declares no ENO. */
static inline void vm_block_set_ENO(vm_block_h b, bool state) {
  if (!b->cfg.eno) return;
  uint8_t v = state ? 1 : 0;
  err_h e = VM_OBJ_SET_VAL_AT(v, b->cfg.eno, 0);
  if (e) {
    SE_push_to_handler(SE_WRAP_ERR_OWNED(OWNER_VM_BLOCK, e, ERR_VM_BLOCK_FAILED, .block_idx = b->cfg.block_idx, .block_type = b->cfg.block_type));
  }
}

/**
 * @brief Run an err_h-returning call inside a block body, reporting failure
 * with this block's identity attached.
 *
 * Block execute() bodies are void -- there is nobody above them to return an
 * err_h to -- so this is where a chain gets reported rather than propagated,
 * the role SE_ORIGIN_CALL plays for top-level entry points. The wrap adds
 * block_idx/block_type, which the accessor-level error underneath cannot know.
 *
 * @code
 * BLOCK_CALL(VM_OBJ_GET_VAL(angle, in0), block);
 * @endcode
 */
#define BLOCK_CALL(call, block)                                                                                                                                       \
  do {                                                                                                                                                                \
    err_h __bc_e = (call);                                                                                                                                            \
    if (__bc_e) {                                                                                                                                                     \
      SE_push_to_handler(SE_WRAP_ERR_OWNED(OWNER_VM_BLOCK, __bc_e, ERR_VM_BLOCK_FAILED, .block_idx = (block)->cfg.block_idx, .block_type = (block)->cfg.block_type)); \
    }                                                                                                                                                                 \
  } while (0)
