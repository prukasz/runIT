#pragma once
#include "vm_expr_core.h"

/*
 *  VM_BLK_EXPR_BIT -- evaluates a bitwise formula over its inputs (uint32): custom_data bytecode on a stack machine, the result to output 0.
 *  The bytecode, its opcodes and the evaluator are shared with the other expression block: vm_expr_core.h.
 */

/* Load-time check of the bytecode. */
bool vm_verify_expr_bit(vm_block_h b);
/* The body, called every pass. */
void vm_blk_expr_bit(vm_block_h b);

//#vm-block VM_BLK_EXPR_BIT @id 2
//@title Bit Expression
//@category data
//@activation triggered Runs when an input it reads is fresh, or on each pass an enable fires (an open gate, a tick), and the block is enabled.
//@data vm_expr_code_t
//@data-tail consts u32[const_cnt], code u8[code_len] (opcodes vm_bit_op_e)
//@opcodes vm_bit_op_e
//@block-description Evaluates a bitwise formula over its inputs, on whole numbers; the result goes to output 0.
//@rule constants and code fit custom_len (4 + 4 x const_cnt + code_len bytes). @error ERR_VM_EXPR_BAD_CODE
//@rule Every opcode is a known value; code after END is ignored. @error ERR_VM_EXPR_BAD_CODE
//@rule An IN operand names a wired input (below in_cnt); a K operand a constant (below const_cnt). @error ERR_VM_EXPR_BAD_CODE
//@rule The stack never holds fewer values than an opcode takes, nor more than VM_EXPR_STACK_MAX. @error ERR_VM_EXPR_BAD_CODE
//@rule The code ends (END or its last byte) with exactly one value on the stack. @error ERR_VM_EXPR_BAD_CODE
//@example bit 3 of a @in 8 @consts 3 @code VM_BIT_IN 0 VM_BIT_K 0 VM_BIT_GET VM_BIT_END @result 1
//@example (a << 4) | b @in 10, 5 @consts 4 @code VM_BIT_IN 0 VM_BIT_K 0 VM_BIT_SHL VM_BIT_IN 1 VM_BIT_OR @result 165
//@example set bits in a @in 0xF0F0 @code VM_BIT_IN 0 VM_BIT_POPCNT @result 8
//@in * pin @title Input @description Read by the IN opcode; any number, only the ones the code names are read. @value u32
//@out 0 result @title Result @description The expression's value (u32). @value u32 @required
