#pragma once
#include "vm_block_helpers.h"
#include "vm_exec.h"

/*
 *           -------------
 *  ->EN     |           | ->ENO
 *  ->START  |    FOR    | ->IDX
 *  ->END    | (SPAN RUN)|
 *  ->STEP   |           |
 *           -------------
 *
 *  VM_BLK_FOR -- C-style float iterator loop with span claiming.
 *  Executes sub-blocks in span range for each turn within max_turns budget.
 */

typedef enum {
  VM_FOR_OP_ADD = 0,
  VM_FOR_OP_SUB,
  VM_FOR_OP_MUL,
  VM_FOR_OP_DIV,
  VM_FOR_OP_CNT
} vm_for_op_e;

typedef enum {
  VM_FOR_CMP_LT = 0,
  VM_FOR_CMP_LE,
  VM_FOR_CMP_GT,
  VM_FOR_CMP_GE,
  VM_FOR_CMP_CNT
} vm_for_cmp_e;

typedef struct vm_for_code_t {
  vm_span_t span;
                       // at most the enclosing loop's end
  float k_start;
  float k_end;
  float k_step;
  uint16_t max_turns;  // Turn budget per pass
  uint8_t op;  // How the iterator advances on each turn
  uint8_t cmp;  // The loop runs while the iterator compares this way with End
  uint8_t rt;
  uint8_t _pad[3];
} vm_for_code_t;

typedef enum vm_in_for_e {
  VM_IN_FOR_START = 0,  // The iterator's first value.
  VM_IN_FOR_END,  // The value the iterator is compared with.
  VM_IN_FOR_STEP,  // What the iterator advances by.
} vm_in_for_e;

typedef enum vm_out_for_e {
  VM_OUT_FOR_INDEX = 0,  // The iterator, published before each turn.
} vm_out_for_e;

_Static_assert(offsetof(vm_for_code_t, span) == 0, "vm_block_span() reads the span off custom_data head");
_Static_assert(offsetof(vm_for_code_t, k_start) % 4 == 0, "literals must stay 4-aligned");
_Static_assert(sizeof(vm_for_code_t) == 24, "wire format header size");
#define VM_FOR_CUSTOM_LEN sizeof(vm_for_code_t)


#define VM_FOR_RT_BAD         0x01u
#define VM_FOR_BAD_CAPPED     0u
#define VM_FOR_BAD_NOT_FINITE 1u

/* Load-time check of the block's state; the pin shape comes from the pin enums above. */
bool vm_verify_for(vm_block_h b);
/* The body, called every pass. */
void vm_blk_for(vm_block_h b);
