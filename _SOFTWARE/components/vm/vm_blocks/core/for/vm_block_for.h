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

//#block-enum @alias Loop Step Operation
typedef enum {
  VM_FOR_OP_ADD = 0,  //@alias +
  VM_FOR_OP_SUB,  //@alias -
  VM_FOR_OP_MUL,  //@alias *
  VM_FOR_OP_DIV,  //@alias /
  VM_FOR_OP_CNT
} vm_for_op_e;

//#block-enum @alias Loop Condition
typedef enum {
  VM_FOR_CMP_LT = 0,  //@alias <
  VM_FOR_CMP_LE,  //@alias <=
  VM_FOR_CMP_GT,  //@alias >
  VM_FOR_CMP_GE,  //@alias >=
  VM_FOR_CMP_CNT
} vm_for_cmp_e;

typedef struct vm_for_code_t {
  vm_span_t span;  //@description Block ids [start, end) this loop runs; start = own id + 1 @derived start = this block's ID + 1; end = ID of the first block after the loop body, at most the enclosing loop's end
                       // at most the enclosing loop's end
  float k_start;  //@description Start value, used while the Start input is unwired
  float k_end;  //@description End value, used while the End input is unwired
  float k_step;  //@description Step, used while the Step input is unwired
  uint16_t max_turns;  //@alias Max turns @description Turn budget per pass
  uint8_t op;  //@alias Step operation @description How the iterator advances on each turn @enum-ref vm_for_op_e
  uint8_t cmp;  //@alias Condition @description The loop runs while the iterator compares this way with End @enum-ref vm_for_cmp_e
  uint8_t rt;  //@runtime
  uint8_t _pad[3];
} vm_for_code_t;

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's tags: @alias (the name a user sees), @value, ... (vm-block-annotations.md).
   An unwired pin uses the state field it @overrides. */
typedef enum vm_in_for_e {
  VM_IN_FOR_START = 0,  //@alias Start @description The iterator's first value. @value f32 @overrides k_start
  VM_IN_FOR_END,  //@alias End @description The value the iterator is compared with. @value f32 @overrides k_end
  VM_IN_FOR_STEP,  //@alias Step @description What the iterator advances by. @value f32 @overrides k_step
} vm_in_for_e;

typedef enum vm_out_for_e {
  VM_OUT_FOR_INDEX = 0,  //@alias Index @description The iterator, published before each turn. @value f32
} vm_out_for_e;

_Static_assert(offsetof(vm_for_code_t, span) == 0, "vm_block_span() reads the span off custom_data head");
_Static_assert(offsetof(vm_for_code_t, k_start) % 4 == 0, "literals must stay 4-aligned");
_Static_assert(sizeof(vm_for_code_t) == 24, "wire format header size");
#define VM_FOR_CUSTOM_LEN sizeof(vm_for_code_t)


#define VM_FOR_RT_BAD 0x01u
#define VM_FOR_BAD_CAPPED 0u
#define VM_FOR_BAD_NOT_FINITE 1u

//#block VM_BLK_FOR
//@alias For
//@category flow
//@description Runs the blocks in its span repeatedly within one pass: for (i = start; i <cmp> end; i = i <op> step), bounded by max_turns.
//@activation enabled
//@when Runs every pass while enabled.
//@face For | {start} to {end} ({cmp}) step {op}{step}
//@eno-alias Loop body
//@eno-description Put a block on its Run when to make it (and what depends on it) part of the loop.
//@data vm_for_code_t
//@rule op is a vm_for_op_e value and cmp a vm_for_cmp_e value. @error ERR_VM_BLK_BAD_SHAPE
//@rule span starts right after the loop block and ends inside the enclosing range (checked when it runs; the body then runs inline). @error ERR_VM_EXEC_BAD_SPAN
/* Load-time check of the block's state; the pin shape comes from the pin enums above. */
bool vm_verify_for(vm_block_h b);
/* The body, called every pass. */
void vm_blk_for(vm_block_h b);
