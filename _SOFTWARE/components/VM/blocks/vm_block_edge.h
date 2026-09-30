#pragma once

#include "vm_block_helpers.h"

/*
 *           -------------
 *  ->EN     |   EDGE    | ->ENO
 *           -------------
 *
 * VM_BLK_EDGE -- one-pass pulse when the combined EN level rises, falls, or
 * changes in either direction. The block samples EN on every pass, including
 * passes when it is false. The first sample establishes the initial level.
 *
 * custom_data layout (4 bytes):
 *   [0] u8 edge_type  vm_edge_type_e
 *   [1] u8 flags      VM_EDGE_F_INITIALIZED (runtime)
 *   [2] u8 previous   previous combined EN level (runtime)
 *   [3] u8 _pad
 */

//#ref-enum @alias Edge Type
typedef enum {
  VM_EDGE_RISING = 0,   // EN changes from false to true
  VM_EDGE_FALLING = 1,  // EN changes from true to false
  VM_EDGE_BOTH = 2,     // EN changes in either direction
  VM_EDGE_TYPE_CNT = 3,
} vm_edge_type_e;

#define VM_EDGE_F_INITIALIZED (1u << 0)

typedef struct __attribute__((aligned(4))) {
  uint8_t edge_type;  // vm_edge_type_e @enum-ref vm_edge_type_e
  uint8_t flags;      // VM_EDGE_F_INITIALIZED @runtime
  uint8_t previous;   // Previous EN level @runtime
  uint8_t _pad;
} vm_block_edge_data_t;

_Static_assert(sizeof(vm_block_edge_data_t) == 4, "vm_block_edge_data_t must be 4 bytes");
#define VM_EDGE_CUSTOM_LEN sizeof(vm_block_edge_data_t)

static inline void vm_edge_init(void* buffer, vm_edge_type_e type) {
  const vm_block_edge_data_t data = {.edge_type = (uint8_t)type};
  memcpy(buffer, &data, sizeof(data));
}

static inline bool vm_verify_edge(vm_block_h b) {
  vm_block_edge_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));
  return state.edge_type < VM_EDGE_TYPE_CNT && b->cfg.en_cnt > 0 && b->cfg.in_cnt == 0 && b->cfg.q_cnt == 0;
}

/* EN is the observed signal, so a false EN must be sampled rather than
 * treated as a reason to skip or reset the block. A failed EN read emits no
 * pulse and leaves the previous sample intact. */
static inline void vm_blk_edge(vm_block_h b) {
  const bool current = vm_block_is_enabled(b);
  if (vm_block_failed(b)) {
    vm_block_set_eno(b, false);
    return;
  }

  vm_block_edge_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));
  bool fired = false;
  if (state.flags & VM_EDGE_F_INITIALIZED) {
    const bool rising = !state.previous && current;
    const bool falling = state.previous && !current;
    fired = state.edge_type == VM_EDGE_RISING ? rising : state.edge_type == VM_EDGE_FALLING ? falling : rising || falling;
  }
  state.previous = current ? 1u : 0u;
  state.flags |= VM_EDGE_F_INITIALIZED;
  memcpy(vm_block_get_custom_data(b), &state, sizeof(state));
  vm_block_set_eno(b, fired);
}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load. */
//#vm-block VM_BLK_EDGE
//@title Edge
//@category logic
//@activation enabled Samples EN every pass, including when EN is false.
//@data vm_block_edge_data_t
//@block-description Pulses ENO for one pass when the combined EN level rises, falls or changes in either direction.
//@header {edge_type} edge
//@rule At least one EN source is connected; no data inputs or outputs. @error ERR_VM_BLK_BAD_SHAPE
//@rule edge_type is a vm_edge_type_e value. @error ERR_VM_BLK_BAD_SHAPE
//@eno @title Pulse @description One-pass pulse when the selected EN edge occurs.
#define VM_BLOCK_TYPE_EDGE \
  {.run = vm_blk_edge, .check = vm_verify_edge, .min_in = 0, .min_q = 0, .required_in = 0x0u, .state_len = VM_EDGE_CUSTOM_LEN}
