#pragma once
#include "vm_expr_core.h"

/*
 *  VM_BLK_EXPR_BIT -- evaluates a bitwise formula over its inputs (uint32): custom_data bytecode on a stack machine, the result to output 0.
 *  The bytecode, its opcodes and the evaluator are shared with the other expression block: vm_expr_core.h.
 */

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's //@in / //@out says what the pin carries and
   which state field it replaces while unwired; titles and descriptions are in expr_bit.display.json. */
//#block-enum @alias Bit Expression Inputs
typedef enum vm_in_expr_bit_e {
  VM_IN_EXPR_BIT_PIN_ANY = 0,   //@in @value u32 @repeat
} vm_in_expr_bit_e;

//#block-enum @alias Bit Expression Outputs
typedef enum vm_out_expr_bit_e {
  VM_OUT_EXPR_BIT_RESULT = 0,   //@out @value u32 @required
} vm_out_expr_bit_e;

/* Load-time check of the block's state; the pin shape comes from the pin enums above. */
//@rule constants and code fit custom_len (4 + 4 x const_cnt + code_len bytes). @error ERR_VM_EXPR_BAD_CODE
//@rule Every opcode is a known value; code after END is ignored. @error ERR_VM_EXPR_BAD_CODE
//@rule An IN operand names a wired input (below in_cnt); a K operand a constant (below const_cnt). @error ERR_VM_EXPR_BAD_CODE
//@rule The stack never holds fewer values than an opcode takes, nor more than VM_EXPR_STACK_MAX. @error ERR_VM_EXPR_BAD_CODE
//@rule The code ends (END or its last byte) with exactly one value on the stack. @error ERR_VM_EXPR_BAD_CODE
bool vm_verify_expr_bit(vm_block_h b);
/* The body, called every pass.
   Face, titles and descriptions: expr_bit.display.json; the app's expr_bit.content.json is generated from this header. */
//#vm-block VM_BLK_EXPR_BIT @id 2
//@activation triggered
//@data vm_expr_code_t
//@data-tail consts u32[const_cnt], code u8[code_len] (opcodes vm_bit_op_e)
//@opcodes vm_bit_op_e @stack-max VM_EXPR_STACK_MAX
//@example bit 3 of a @in 8 @consts 3 @code VM_BIT_IN 0 VM_BIT_K 0 VM_BIT_GET VM_BIT_END @result 1
//@example (a << 4) | b @in 10, 5 @consts 4 @code VM_BIT_IN 0 VM_BIT_K 0 VM_BIT_SHL VM_BIT_IN 1 VM_BIT_OR @result 165
//@example set bits in a @in 0xF0F0 @code VM_BIT_IN 0 VM_BIT_POPCNT @result 8
void vm_blk_expr_bit(vm_block_h b);
