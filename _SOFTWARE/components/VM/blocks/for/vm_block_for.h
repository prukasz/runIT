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

//#ref-enum @alias Loop Step Operation
typedef enum vm_for_op_e {
  VM_FOR_OP_ADD = 0,  //@alias +
  VM_FOR_OP_SUB,  //@alias -
  VM_FOR_OP_MUL,  //@alias *
  VM_FOR_OP_DIV,  //@alias /
  VM_FOR_OP_CNT
} vm_for_op_e;

//#ref-enum @alias Loop Condition
typedef enum vm_for_cmp_e {
  VM_FOR_CMP_LT = 0,  //@alias <
  VM_FOR_CMP_LE,  //@alias <=
  VM_FOR_CMP_GT,  //@alias >
  VM_FOR_CMP_GE,  //@alias >=
  VM_FOR_CMP_CNT
} vm_for_cmp_e;

typedef struct vm_for_code_t {
  vm_span_t span;      // Block ids [start, end) this loop runs; start = own id + 1 @derived start = this block's ID + 1; end = ID of the first block after the loop body, at most the enclosing loop's end
  float k_start;       // Start when input 0 is unwired
  float k_end;         // End when input 1 is unwired
  float k_step;        // Step when input 2 is unwired
  uint16_t max_turns;  // Turn budget per pass
  uint8_t op;          // How the iterator advances @enum-ref vm_for_op_e
  uint8_t cmp;         // Loop condition @enum-ref vm_for_cmp_e
  uint8_t rt;          // @runtime
  uint8_t _pad[3];
} vm_for_code_t;

_Static_assert(offsetof(vm_for_code_t, span) == 0, "vm_block_span() reads the span off custom_data head");
_Static_assert(offsetof(vm_for_code_t, k_start) % 4 == 0, "literals must stay 4-aligned");
_Static_assert(sizeof(vm_for_code_t) == 24, "wire format header size");
#define VM_FOR_CUSTOM_LEN sizeof(vm_for_code_t)

#define VM_FOR_IN_START 0u
#define VM_FOR_IN_END 1u
#define VM_FOR_IN_STEP 2u

#define VM_FOR_RT_BAD 0x01u
#define VM_FOR_BAD_CAPPED 0u
#define VM_FOR_BAD_NOT_FINITE 1u

/* Load-time check of the block's state; the pin shape is checked from the //@in / //@out directives below. */
bool vm_verify_for(vm_block_h b);
/* The body, called every pass. */
void vm_blk_for(vm_block_h b);

//#vm-block VM_BLK_FOR @id 5
//@title For
//@category flow
//@activation enabled Runs every pass while enabled.
//@data vm_for_code_t
//@block-description Runs the blocks in its span repeatedly within one pass: for (i = start; i <cmp> end; i = i <op> step), bounded by max_turns.
//@header For | {start} to {end} ({cmp}) step {op}{step}
//@rule op is a vm_for_op_e value and cmp a vm_for_cmp_e value. @error ERR_VM_BLK_BAD_SHAPE
//@rule span starts right after the loop block and ends inside the enclosing range (checked when it runs; the body then runs inline). @error ERR_VM_EXEC_BAD_SPAN
//@in 0 start @title Start @description The iterator's first value. @value f32 @overrides k_start @macro VM_FOR_IN_START
//@in 1 end @title End @description The value the iterator is compared with. @value f32 @overrides k_end @macro VM_FOR_IN_END
//@in 2 step @title Step @description What the iterator advances by. @value f32 @overrides k_step @macro VM_FOR_IN_STEP
//@out 0 index @title Index @description The iterator, published before each turn. @value f32
//@eno @title Loop body @description Put a block on its Run when to make it (and what depends on it) part of the loop.
