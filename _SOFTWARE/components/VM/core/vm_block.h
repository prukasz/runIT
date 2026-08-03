#pragma once
#include <stddef.h>
#include <stdint.h>
#include "vm_obj_access.h"

/*
One block = one linear-allocator blob, sized once at creation:
  [cfg][in_cnt * vm_accessor_t*][q_cnt * vm_accessor_t*][custom_data bytes]

No separate allocations for inputs/outputs/custom_data, and nothing to keep
in sync -- their positions are derived from in_cnt/q_cnt, not stored.
*/

typedef struct vm_block_data_t {
  struct {
    uint16_t block_idx;
    uint16_t node_idx;
    uint16_t connected_in;  // bitmask of used inputs (for hardoced inputs - blocks)
    uint8_t block_type;
    uint8_t in_cnt;
    uint8_t q_cnt;
    uint8_t status;
  } cfg;
  uint8_t data[];
} vm_block_data_t;

static inline vm_accessor_t** vm_block_inputs(vm_block_data_t* b) {
  return (vm_accessor_t**)b->data;
}

static inline vm_accessor_t** vm_block_outputs(vm_block_data_t* b) {
  return vm_block_inputs(b) + b->cfg.in_cnt;
}

static inline void* vm_block_custom_data(vm_block_data_t* b) {
  return (void*)(vm_block_outputs(b) + b->cfg.q_cnt);
}

// total bytes required for one block of this shape -- what the loader uses
// to size a single linear-allocator slot before writing into it
static inline size_t vm_block_size(uint8_t in_cnt, uint8_t q_cnt, uint16_t custom_len) {
  return sizeof(vm_block_data_t) + (size_t)(in_cnt + q_cnt) * sizeof(vm_accessor_t*) + custom_len;
}
