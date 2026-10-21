#pragma once
#include "vm_block_helpers.h"

/*
 *           -------------
 *  ->EN     |           | ->ENO
 * (->SOURCE)|    SET    |
 *  ->DST    |           |
 *           -------------
 *
 *  VM_BLK_SET -- EN -> DST = SOURCE, on every pass the block is enabled.
 *  Copies whole trees into an existing matching shape without allocation. Types and
 *  element counts must match exactly (no conversion). Both pins are inputs; ENO
 *  signals a write completed this pass.
 *
 *  Constant mode: with SOURCE unwired the block writes its `value` constant to the first
 *  scalar of DST (converted to DST's type: rounded, saturated) instead of copying.
 */

typedef struct __attribute__((aligned(4))) {
  float   value;
  uint8_t _pad[4];
} vm_block_set_data_t;

_Static_assert(sizeof(vm_block_set_data_t) == 8, "vm_block_set_data_t must be 8 bytes");
#define VM_SET_CUSTOM_LEN sizeof(vm_block_set_data_t)

typedef enum vm_in_set_e {
  VM_IN_SET_SOURCE = 0,  // The object to copy. Leave it unwired to write the Value constant instead.
  VM_IN_SET_DESTINATION,  // The object to write.
} vm_in_set_e;

/* Load-time check of the block's state; the pin shape comes from the pin enum above. */
bool vm_verify_set(vm_block_h b);
/* The body, called every pass. */
void vm_blk_set(vm_block_h b);
