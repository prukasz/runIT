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
typedef enum vm_for_op_e {
  VM_FOR_OP_ADD = 0,  //@alias +
  VM_FOR_OP_SUB,  //@alias -
  VM_FOR_OP_MUL,  //@alias *
  VM_FOR_OP_DIV,  //@alias /
  VM_FOR_OP_CNT
} vm_for_op_e;

//#block-enum @alias Loop Condition
typedef enum vm_for_cmp_e {
  VM_FOR_CMP_LT = 0,  //@alias <
  VM_FOR_CMP_LE,  //@alias <=
  VM_FOR_CMP_GT,  //@alias >
  VM_FOR_CMP_GE,  //@alias >=
  VM_FOR_CMP_CNT
} vm_for_cmp_e;

//@data vm_for_code_t
typedef struct vm_for_code_t {
  vm_span_t span;      // @description Block ids [start, end) this loop runs; start = own id + 1
                       // @derived start = this block's ID + 1; end = ID of the first block after the loop body,
                       // at most the enclosing loop's end
  float k_start;       // @description Start when input 0 is unwired
  float k_end;         // @description End when input 1 is unwired
  float k_step;        // @description Step when input 2 is unwired
  uint16_t max_turns;  // @description Turn budget per pass
  uint8_t op;          // @description How the iterator advances @enum-ref vm_for_op_e
  uint8_t cmp;         // @description Loop condition @enum-ref vm_for_cmp_e
  uint8_t rt;          // @runtime
  uint8_t _pad[3];
} vm_for_code_t;

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's //@in / //@out says what the pin carries and which
   state field it replaces while unwired; titles and descriptions are in for.display.json. */
//#block-enum @alias For Inputs
typedef enum vm_in_for_e {
  VM_IN_FOR_START = 0,  //@in @value f32 @overrides k_start
  VM_IN_FOR_END,        //@in @value f32 @overrides k_end
  VM_IN_FOR_STEP,       //@in @value f32 @overrides k_step
} vm_in_for_e;

//#block-enum @alias For Outputs
typedef enum vm_out_for_e {
  VM_OUT_FOR_INDEX = 0,  //@out @value f32
} vm_out_for_e;

_Static_assert(offsetof(vm_for_code_t, span) == 0, "vm_block_span() reads the span off custom_data head");
_Static_assert(offsetof(vm_for_code_t, k_start) % 4 == 0, "literals must stay 4-aligned");
_Static_assert(sizeof(vm_for_code_t) == 24, "wire format header size");
#define VM_FOR_CUSTOM_LEN sizeof(vm_for_code_t)


#define VM_FOR_RT_BAD 0x01u
#define VM_FOR_BAD_CAPPED 0u
#define VM_FOR_BAD_NOT_FINITE 1u

/* Load-time check of the block's state; the pin shape comes from the pin enums above. */
//@rule op is a vm_for_op_e value and cmp a vm_for_cmp_e value. @error ERR_VM_BLK_BAD_SHAPE
bool vm_verify_for(vm_block_h b);
/* The body, called every pass.
   Face, titles and descriptions: for.display.json; the app's for.content.json is generated from this header. */
//#vm-block VM_BLK_FOR @id 5
//@activation enabled
//@rule span starts right after the loop block and ends inside the enclosing range (checked when it runs; the body then runs inline). @error ERR_VM_EXEC_BAD_SPAN
void vm_blk_for(vm_block_h b);
