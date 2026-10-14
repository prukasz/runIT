#pragma once
#include "vm_expr_core.h"

/*
 *  VM_BLK_EXPR -- evaluates a float formula over its inputs: custom_data bytecode on a stack machine, the result to output 0.
 *  The bytecode, its opcodes and the evaluator are shared with the other expression block: vm_expr_core.h.
 */

typedef enum vm_in_expr_e {
  VM_IN_EXPR_PIN_ANY = 0,  // Read by the IN opcode; any number, only the ones the code names are read.
} vm_in_expr_e;

typedef enum vm_out_expr_e {
  VM_OUT_EXPR_RESULT = 0,  // The expression's value (float).
} vm_out_expr_e;

/* Load-time check of the block's state; the pin shape comes from the pin enums above. */
bool vm_verify_expr(vm_block_h b);
/* The body, called every pass. */
void vm_blk_expr(vm_block_h b);
