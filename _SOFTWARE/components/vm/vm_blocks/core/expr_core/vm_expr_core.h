#pragma once

#include "vm_block.h"

/*
 *           -------------
 *  ->EN     |   EXPR    | ->ENO
 *  ->IN0..n | EXPR_BIT  | ->Q
 *           -------------
 *
 *  RPN Bytecode Expression Engine (VM_BLK_EXPR for float, VM_BLK_EXPR_BIT for uint32): the part both blocks share.
 *  Evaluates custom_data bytecode using a stack machine and writes the result to Q.
 *
 *  The bytecode is checked once, at load (vm_verify_expr / vm_verify_expr_bit):
 *  every opcode known, every operand in range and wired, the stack never under-
 *  or overflows and ends with exactly one value. The evaluator trusts that and
 *  only checks values (division by zero, domain, non-finite result).
 */

#define VM_EXPR_STACK_MAX 16

//#vm-type vm_expr_k_t @size 4 @align 4
typedef union vm_expr_k_t {
  uint32_t u;
  float f;
} vm_expr_k_t;
_Static_assert(sizeof(vm_expr_k_t) == 4 && _Alignof(vm_expr_k_t) == 4, "vm_expr_k_t layout");

typedef struct vm_expr_code_t {
  uint8_t const_cnt;
  uint8_t rt;
  uint16_t code_len;
  vm_expr_k_t consts[];
} vm_expr_code_t;

_Static_assert(sizeof(vm_expr_code_t) == 4, "header must stay 4 bytes for literal alignment");

#define VM_EXPR_RT_FAULTED 0x01u

static inline size_t vm_expr_size(uint8_t const_cnt, uint16_t code_len) {
  return sizeof(vm_expr_code_t) + (size_t)const_cnt * sizeof(vm_expr_k_t) + (size_t)code_len;
}

static inline const uint8_t* vm_expr_bytecode(const vm_expr_code_t* c) {
  return (const uint8_t*)&c->consts[c->const_cnt];
}

/* ==========================================================================
   Opcodes -- VM_BLK_EXPR (float). Stack notation: before -> after, top last.
   ========================================================================== */
//#ref-enum @alias Expression Opcode
typedef enum vm_expr_op_e {
  VM_EXPR_END = 0,  //@alias end @description Stop; the rest of the code is ignored.
  VM_EXPR_IN,       //@alias in @description -> input[operand] (as float) @format {input}
  VM_EXPR_K,        //@alias const @description -> constant[operand] @format {const}
  VM_EXPR_DUP,      //@alias dup @description a -> a a @stack-op
  VM_EXPR_DROP,     //@alias drop @description a -> @stack-op
  VM_EXPR_SWAP,     //@alias swap @description a b -> b a @stack-op

  // binary arithmetic
  VM_EXPR_ADD,    //@alias + @description a b -> a + b @format {a} + {b}
  VM_EXPR_SUB,    //@alias - @description a b -> a - b @format {a} - {b}
  VM_EXPR_MUL,    //@alias * @description a b -> a * b @format {a} * {b}
  VM_EXPR_DIV,    //@alias / @description a b -> a / b (b = 0 faults) @format {a} / {b}
  VM_EXPR_MOD,    //@alias mod @description a b -> fmod(a, b) (b = 0 faults) @format {a} mod {b}
  VM_EXPR_POW,    //@alias pow @description a b -> a to the power b (a < 0 with fractional b faults) @format {a} ^ {b}
  VM_EXPR_ROOT,   //@alias root @description a b -> b-th root of a (b = 0 faults; a < 0 needs an odd integer b) @format root({a}, {b})
  VM_EXPR_MIN,    //@alias min @description a b -> the smaller @format min({a}, {b})
  VM_EXPR_MAX,    //@alias max @description a b -> the larger @format max({a}, {b})
  VM_EXPR_ATAN2,  //@alias atan2 @description y x -> atan2(y, x), radians @format atan2({a}, {b})
  VM_EXPR_HYPOT,  //@alias hypot @description a b -> sqrt(a*a + b*b) @format hypot({a}, {b})

  // unary
  VM_EXPR_NEG,     //@alias neg @description a -> -a @format -{a}
  VM_EXPR_ABS,     //@alias abs @description a -> |a| @format abs({a})
  VM_EXPR_SQRT,    //@alias sqrt @description a -> square root (a < 0 faults) @format sqrt({a})
  VM_EXPR_SQUARE,  //@alias sq @description a -> a * a @format ({a})^2
  VM_EXPR_RECIP,   //@alias 1/x @description a -> 1 / a (a = 0 faults) @format 1 / {a}
  VM_EXPR_FLOOR,   //@alias floor @description a -> round down @format floor({a})
  VM_EXPR_CEIL,    //@alias ceil @description a -> round up @format ceil({a})
  VM_EXPR_ROUND,   //@alias round @description a -> round half away from zero @format round({a})
  VM_EXPR_TRUNC,   //@alias trunc @description a -> round toward zero @format trunc({a})
  VM_EXPR_SIGN,    //@alias sign @description a -> -1, 0 or 1 @format sign({a})
  VM_EXPR_SIN,     //@alias sin @description a -> sin(a), radians @format sin({a})
  VM_EXPR_COS,     //@alias cos @description a -> cos(a), radians @format cos({a})
  VM_EXPR_TAN,     //@alias tan @description a -> tan(a), radians @format tan({a})
  VM_EXPR_ASIN,    //@alias asin @description a -> asin(a) (|a| > 1 faults) @format asin({a})
  VM_EXPR_ACOS,    //@alias acos @description a -> acos(a) (|a| > 1 faults) @format acos({a})
  VM_EXPR_ATAN,    //@alias atan @description a -> atan(a) @format atan({a})
  VM_EXPR_EXP,     //@alias exp @description a -> e to the power a @format exp({a})
  VM_EXPR_LOG,     //@alias ln @description a -> natural log (a <= 0 faults) @format ln({a})
  VM_EXPR_LOG10,   //@alias log10 @description a -> base-10 log (a <= 0 faults) @format log10({a})
  VM_EXPR_LOG2,    //@alias log2 @description a -> base-2 log (a <= 0 faults) @format log2({a})
  VM_EXPR_DEG,     //@alias deg @description a -> radians to degrees @format deg({a})
  VM_EXPR_RAD,     //@alias rad @description a -> degrees to radians @format rad({a})

  // comparison (1 or 0)
  VM_EXPR_LT,  //@alias < @description a b -> a < b @format {a} < {b}
  VM_EXPR_LE,  //@alias <= @description a b -> a <= b @format {a} <= {b}
  VM_EXPR_GT,  //@alias > @description a b -> a > b @format {a} > {b}
  VM_EXPR_GE,  //@alias >= @description a b -> a >= b @format {a} >= {b}
  VM_EXPR_EQ,  //@alias == @description a b -> a == b (exact) @format {a} == {b}
  VM_EXPR_NE,  //@alias != @description a b -> a != b (exact) @format {a} != {b}

  // logic (non-zero is true; 1 or 0)
  VM_EXPR_AND,  //@alias and @description a b -> a and b @format {a} and {b}
  VM_EXPR_OR,   //@alias or @description a b -> a or b @format {a} or {b}
  VM_EXPR_XOR,  //@alias xor @description a b -> exactly one of a, b @format {a} xor {b}
  VM_EXPR_NOT,  //@alias not @description a -> not a @format not {a}

  VM_EXPR_SEL,  //@alias select @description c t f -> t if c is non-zero, else f @format {a} ? {b} : {c}
  VM_EXPR_OP_CNT
} vm_expr_op_e;

/* ==========================================================================
   Opcodes -- VM_BLK_EXPR_BIT (uint32)
   ========================================================================== */
//#ref-enum @alias Bit Expression Opcode
typedef enum vm_bit_op_e {
  VM_BIT_END = 0,  //@alias end @description Stop; the rest of the code is ignored.
  VM_BIT_IN,       //@alias in @description -> input[operand] (as u32) @format {input}
  VM_BIT_K,        //@alias const @description -> constant[operand] @format {const}
  VM_BIT_DUP,      //@alias dup @description a -> a a @stack-op
  VM_BIT_DROP,     //@alias drop @description a -> @stack-op
  VM_BIT_SWAP,     //@alias swap @description a b -> b a @stack-op

  // binary
  VM_BIT_AND,  //@alias & @description a b -> a AND b @format {a} & {b}
  VM_BIT_OR,   //@alias | @description a b -> a OR b @format {a} | {b}
  VM_BIT_XOR,  //@alias ^ @description a b -> a XOR b @format {a} ^ {b}
  VM_BIT_SHL,  //@alias << @description a n -> a shifted left by n (n >= 32 gives 0) @format {a} << {b}
  VM_BIT_SHR,  //@alias >> @description a n -> a shifted right by n, zero fill (n >= 32 gives 0) @format {a} >> {b}
  VM_BIT_SAR,  //@alias >>> @description a n -> a shifted right by n, sign fill @format {a} >>> {b}
  VM_BIT_ROL,  //@alias rol @description a n -> a rotated left by n mod 32 @format rol({a}, {b})
  VM_BIT_ROR,  //@alias ror @description a n -> a rotated right by n mod 32 @format ror({a}, {b})
  VM_BIT_GET,  //@alias bit @description a n -> bit n mod 32 of a (1 or 0) @format bit({a}, {b})
  VM_BIT_SET,  //@alias set @description a n -> a with bit n mod 32 set @format setbit({a}, {b})
  VM_BIT_CLR,  //@alias clear @description a n -> a with bit n mod 32 cleared @format clearbit({a}, {b})
  VM_BIT_TGL,  //@alias toggle @description a n -> a with bit n mod 32 flipped @format togglebit({a}, {b})

  // unary
  VM_BIT_NOT,     //@alias ~ @description a -> every bit flipped @format ~{a}
  VM_BIT_POPCNT,  //@alias popcount @description a -> number of set bits @format popcnt({a})
  VM_BIT_CLZ,     //@alias clz @description a -> leading zero bits (32 for 0) @format clz({a})
  VM_BIT_CTZ,     //@alias ctz @description a -> trailing zero bits (32 for 0) @format ctz({a})
  VM_BIT_BSWAP,   //@alias bswap @description a -> byte order reversed @format bswap({a})

  VM_BIT_OP_CNT
} vm_bit_op_e;

/* ==========================================================================
   Opcode table: stack effect and operand, per opcode. The load-time check
   walks the code with it; generate-vm-blocks.py publishes it.
   ========================================================================== */
#define VM_EXPR_ARG_NONE  0u  // no operand byte
#define VM_EXPR_ARG_INPUT 1u  // operand: input pin index (< in_cnt, wired)
#define VM_EXPR_ARG_CONST 2u  // operand: constant index (< const_cnt)

typedef struct {
  uint8_t pops;
  uint8_t pushes;
  uint8_t arg;  // VM_EXPR_ARG_*
} vm_expr_op_info_t;

static const vm_expr_op_info_t vm_expr_ops[VM_EXPR_OP_CNT] = {
    [VM_EXPR_END] = {0, 0, VM_EXPR_ARG_NONE},    [VM_EXPR_IN] = {0, 1, VM_EXPR_ARG_INPUT},
    [VM_EXPR_K] = {0, 1, VM_EXPR_ARG_CONST},     [VM_EXPR_DUP] = {1, 2, VM_EXPR_ARG_NONE},
    [VM_EXPR_DROP] = {1, 0, VM_EXPR_ARG_NONE},   [VM_EXPR_SWAP] = {2, 2, VM_EXPR_ARG_NONE},
    [VM_EXPR_ADD] = {2, 1, VM_EXPR_ARG_NONE},    [VM_EXPR_SUB] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_MUL] = {2, 1, VM_EXPR_ARG_NONE},    [VM_EXPR_DIV] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_MOD] = {2, 1, VM_EXPR_ARG_NONE},    [VM_EXPR_POW] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_ROOT] = {2, 1, VM_EXPR_ARG_NONE},   [VM_EXPR_MIN] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_MAX] = {2, 1, VM_EXPR_ARG_NONE},    [VM_EXPR_ATAN2] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_HYPOT] = {2, 1, VM_EXPR_ARG_NONE},  [VM_EXPR_NEG] = {1, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_ABS] = {1, 1, VM_EXPR_ARG_NONE},    [VM_EXPR_SQRT] = {1, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_SQUARE] = {1, 1, VM_EXPR_ARG_NONE}, [VM_EXPR_RECIP] = {1, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_FLOOR] = {1, 1, VM_EXPR_ARG_NONE},  [VM_EXPR_CEIL] = {1, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_ROUND] = {1, 1, VM_EXPR_ARG_NONE},  [VM_EXPR_TRUNC] = {1, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_SIGN] = {1, 1, VM_EXPR_ARG_NONE},   [VM_EXPR_SIN] = {1, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_COS] = {1, 1, VM_EXPR_ARG_NONE},    [VM_EXPR_TAN] = {1, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_ASIN] = {1, 1, VM_EXPR_ARG_NONE},   [VM_EXPR_ACOS] = {1, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_ATAN] = {1, 1, VM_EXPR_ARG_NONE},   [VM_EXPR_EXP] = {1, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_LOG] = {1, 1, VM_EXPR_ARG_NONE},    [VM_EXPR_LOG10] = {1, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_LOG2] = {1, 1, VM_EXPR_ARG_NONE},   [VM_EXPR_DEG] = {1, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_RAD] = {1, 1, VM_EXPR_ARG_NONE},    [VM_EXPR_LT] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_LE] = {2, 1, VM_EXPR_ARG_NONE},     [VM_EXPR_GT] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_GE] = {2, 1, VM_EXPR_ARG_NONE},     [VM_EXPR_EQ] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_NE] = {2, 1, VM_EXPR_ARG_NONE},     [VM_EXPR_AND] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_OR] = {2, 1, VM_EXPR_ARG_NONE},     [VM_EXPR_XOR] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_EXPR_NOT] = {1, 1, VM_EXPR_ARG_NONE},    [VM_EXPR_SEL] = {3, 1, VM_EXPR_ARG_NONE},
};

static const vm_expr_op_info_t vm_bit_ops[VM_BIT_OP_CNT] = {
    [VM_BIT_END] = {0, 0, VM_EXPR_ARG_NONE},    [VM_BIT_IN] = {0, 1, VM_EXPR_ARG_INPUT},
    [VM_BIT_K] = {0, 1, VM_EXPR_ARG_CONST},     [VM_BIT_DUP] = {1, 2, VM_EXPR_ARG_NONE},
    [VM_BIT_DROP] = {1, 0, VM_EXPR_ARG_NONE},   [VM_BIT_SWAP] = {2, 2, VM_EXPR_ARG_NONE},
    [VM_BIT_AND] = {2, 1, VM_EXPR_ARG_NONE},    [VM_BIT_OR] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_BIT_XOR] = {2, 1, VM_EXPR_ARG_NONE},    [VM_BIT_SHL] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_BIT_SHR] = {2, 1, VM_EXPR_ARG_NONE},    [VM_BIT_SAR] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_BIT_ROL] = {2, 1, VM_EXPR_ARG_NONE},    [VM_BIT_ROR] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_BIT_GET] = {2, 1, VM_EXPR_ARG_NONE},    [VM_BIT_SET] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_BIT_CLR] = {2, 1, VM_EXPR_ARG_NONE},    [VM_BIT_TGL] = {2, 1, VM_EXPR_ARG_NONE},
    [VM_BIT_NOT] = {1, 1, VM_EXPR_ARG_NONE},    [VM_BIT_POPCNT] = {1, 1, VM_EXPR_ARG_NONE},
    [VM_BIT_CLZ] = {1, 1, VM_EXPR_ARG_NONE},    [VM_BIT_CTZ] = {1, 1, VM_EXPR_ARG_NONE},
    [VM_BIT_BSWAP] = {1, 1, VM_EXPR_ARG_NONE},
};

/* ==========================================================================
   Fault reasons (ERR_VM_EXPR_BAD_CODE at load, ERR_VM_EXPR_MATH at run time)
   ========================================================================== */
#define VM_EXPR_BAD_HEADER 0u     // constants + code don't fit custom_len
#define VM_EXPR_BAD_OPCODE 1u     // unknown opcode
#define VM_EXPR_BAD_OPERAND 2u    // operand missing, out of range, or an unwired input
#define VM_EXPR_BAD_UNDERFLOW 3u  // an opcode needs more values than the stack holds
#define VM_EXPR_BAD_OVERFLOW 4u   // more than VM_EXPR_STACK_MAX values
#define VM_EXPR_BAD_RESULT 5u     // the stack doesn't end with exactly one value

#define VM_EXPR_MATH_DIV0 0u
#define VM_EXPR_MATH_DOMAIN 1u
#define VM_EXPR_MATH_NOT_FINITE 2u

/* Load-time check of a block's bytecode against an opcode table (the blocks' vm_verify_* call it). */
bool vm_expr_check(vm_block_h b, const vm_expr_op_info_t* ops, uint8_t op_cnt);

/* The evaluators: only run code vm_expr_check() accepted. False when the block failed (reported). */
bool vm_expr_eval_f(vm_block_h b, vm_expr_code_t* c, float* out);
bool vm_expr_eval_bit(vm_block_h b, vm_expr_code_t* c, uint32_t* out);
