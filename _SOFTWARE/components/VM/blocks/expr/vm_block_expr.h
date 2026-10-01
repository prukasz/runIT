#pragma once
#include "vm_expr_core.h"

/*
 *  VM_BLK_EXPR -- evaluates a float formula over its inputs: custom_data bytecode on a stack machine, the result to output 0.
 *  The bytecode, its opcodes and the evaluator are shared with the other expression block: vm_expr_core.h.
 */

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's //@in / //@out says what the pin carries and
   which state field it replaces while unwired; titles and descriptions are in expr.display.json. */
//#block-enum @alias Expression Inputs
typedef enum vm_in_expr_e {
  VM_IN_EXPR_PIN_ANY = 0,   //@in @value f32 @repeat
} vm_in_expr_e;

//#block-enum @alias Expression Outputs
typedef enum vm_out_expr_e {
  VM_OUT_EXPR_RESULT = 0,   //@out @value f32 @required
} vm_out_expr_e;

/* Load-time check of the block's state; the pin shape comes from the pin enums above. */
//@rule constants and code fit custom_len (4 + 4 x const_cnt + code_len bytes). @error ERR_VM_EXPR_BAD_CODE
//@rule Every opcode is a known value; code after END is ignored. @error ERR_VM_EXPR_BAD_CODE
//@rule An IN operand names a wired input (below in_cnt); a K operand a constant (below const_cnt). @error ERR_VM_EXPR_BAD_CODE
//@rule The stack never holds fewer values than an opcode takes, nor more than VM_EXPR_STACK_MAX. @error ERR_VM_EXPR_BAD_CODE
//@rule The code ends (END or its last byte) with exactly one value on the stack. @error ERR_VM_EXPR_BAD_CODE
bool vm_verify_expr(vm_block_h b);
/* The body, called every pass.
   Face, titles and descriptions: expr.display.json; the app's expr.content.json is generated from this header. */
//#vm-block VM_BLK_EXPR @id 1
//@activation triggered
//@data vm_expr_code_t
//@data-tail consts u32[const_cnt], code u8[code_len] (opcodes vm_expr_op_e)
//@opcodes vm_expr_op_e @stack-max VM_EXPR_STACK_MAX
//@example (a + 2) * b @in 3, 4 @consts 2 @code VM_EXPR_IN 0 VM_EXPR_K 0 VM_EXPR_ADD VM_EXPR_IN 1 VM_EXPR_MUL VM_EXPR_END @result 20
//@example larger of a and b @in 5, 7 @code VM_EXPR_IN 0 VM_EXPR_IN 1 VM_EXPR_GT VM_EXPR_IN 0 VM_EXPR_IN 1 VM_EXPR_SEL @result 7
//@example clamp a to 0..100 @in 150 @consts 0, 100 @code VM_EXPR_IN 0 VM_EXPR_K 0 VM_EXPR_MAX VM_EXPR_K 1 VM_EXPR_MIN @result 100
//@example length of (a, b) @in 3, 4 @code VM_EXPR_IN 0 VM_EXPR_IN 1 VM_EXPR_HYPOT @result 5
void vm_blk_expr(vm_block_h b);
