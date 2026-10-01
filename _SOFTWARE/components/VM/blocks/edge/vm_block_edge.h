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

/* Load-time check of the block's state; the pin shape is checked from the //@in / //@out directives below. */
bool vm_verify_edge(vm_block_h b);
/* The body, called every pass. */
void vm_blk_edge(vm_block_h b);

//#vm-block VM_BLK_EDGE @id 8
//@title Edge
//@category logic
//@activation enabled Samples EN every pass, including when EN is false.
//@enables required
//@data vm_block_edge_data_t
//@block-description Pulses ENO for one pass when the combined EN level rises, falls or changes in either direction.
//@header {edge_type} edge
//@rule At least one EN source is connected; no data inputs or outputs. @error ERR_VM_BLK_BAD_SHAPE
//@rule edge_type is a vm_edge_type_e value. @error ERR_VM_BLK_BAD_SHAPE
//@eno @title Pulse @description One-pass pulse when the selected EN edge occurs.
