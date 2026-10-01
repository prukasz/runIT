#pragma once
#include "vm_expr_core.h"

/*
 *  VM_BLK_EXPR -- evaluates a float formula over its inputs: custom_data bytecode on a stack machine, the result to output 0.
 *  The bytecode, its opcodes and the evaluator are shared with the other expression block: vm_expr_core.h.
 */

/* Load-time check of the bytecode. */
bool vm_verify_expr(vm_block_h b);
/* The body, called every pass. */
void vm_blk_expr(vm_block_h b);

//#vm-block VM_BLK_EXPR @id 1
//@title Expression
//@category data
//@activation triggered Runs when an input it reads is fresh, or on each pass an enable fires (an open gate, a tick), and the block is enabled.
//@data vm_expr_code_t
//@data-tail consts u32[const_cnt], code u8[code_len] (opcodes vm_expr_op_e)
//@opcodes vm_expr_op_e
//@block-description Evaluates a numeric or logical formula over its inputs; the result goes to output 0.
//@rule constants and code fit custom_len (4 + 4 x const_cnt + code_len bytes). @error ERR_VM_EXPR_BAD_CODE
//@rule Every opcode is a known value; code after END is ignored. @error ERR_VM_EXPR_BAD_CODE
//@rule An IN operand names a wired input (below in_cnt); a K operand a constant (below const_cnt). @error ERR_VM_EXPR_BAD_CODE
//@rule The stack never holds fewer values than an opcode takes, nor more than VM_EXPR_STACK_MAX. @error ERR_VM_EXPR_BAD_CODE
//@rule The code ends (END or its last byte) with exactly one value on the stack. @error ERR_VM_EXPR_BAD_CODE
//@example (a + 2) * b @in 3, 4 @consts 2 @code VM_EXPR_IN 0 VM_EXPR_K 0 VM_EXPR_ADD VM_EXPR_IN 1 VM_EXPR_MUL VM_EXPR_END @result 20
//@example larger of a and b @in 5, 7 @code VM_EXPR_IN 0 VM_EXPR_IN 1 VM_EXPR_GT VM_EXPR_IN 0 VM_EXPR_IN 1 VM_EXPR_SEL @result 7
//@example clamp a to 0..100 @in 150 @consts 0, 100 @code VM_EXPR_IN 0 VM_EXPR_K 0 VM_EXPR_MAX VM_EXPR_K 1 VM_EXPR_MIN @result 100
//@example length of (a, b) @in 3, 4 @code VM_EXPR_IN 0 VM_EXPR_IN 1 VM_EXPR_HYPOT @result 5
//@in * pin @title Input @description Read by the IN opcode; any number, only the ones the code names are read. @value f32
//@out 0 result @title Result @description The expression's value (float). @value f32 @required
