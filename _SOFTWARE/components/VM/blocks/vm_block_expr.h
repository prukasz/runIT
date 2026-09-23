#pragma once
#include <math.h>
#include "vm_block.h"

/*
 *           -------------
 *  ->EN     |   EXPR    | ->ENO
 *  ->IN0..n | EXPR_BIT  | ->Q
 *           -------------
 *
 *  RPN Bytecode Expression Engine (VM_BLK_EXPR for float, VM_BLK_EXPR_BIT for uint32).
 *  Evaluates custom_data bytecode using a stack machine and writes the result to Q.
 *
 *  custom_data layout:
 *    [0]     u8  const_cnt         Literals count following header
 *    [1]     u8  rt                Runtime latch (VM_EXPR_RT_FAULTED; 0 on wire)
 *    [2..3]  u16 code_len          Bytecode length in bytes
 *    [4..]   u32 consts[const_cnt] 4-aligned literals
 *    [...]   u8  code[code_len]    RPN instruction stream (IN and K have +1 operand byte)
 *
 *  The bytecode is checked once, at load (vm_verify_expr / vm_verify_expr_bit):
 *  every opcode known, every operand in range and wired, the stack never under-
 *  or overflows and ends with exactly one value. The evaluator trusts that and
 *  only checks values (division by zero, domain, non-finite result).
 */

#define VM_EXPR_STACK_MAX 16

typedef union vm_expr_k_t {
  uint32_t u;
  float f;
} vm_expr_k_t;

typedef struct vm_expr_code_t {
  uint8_t const_cnt;     // Number of u32 literals after the header
  uint8_t rt;            // @runtime
  uint16_t code_len;     // Bytecode length after the literals
  vm_expr_k_t consts[];  // const_cnt literals (f32 for EXPR, u32 for EXPR_BIT), then code_len opcode bytes
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
  VM_EXPR_IN,       //@alias in @description -> input[operand] (as float)
  VM_EXPR_K,        //@alias const @description -> constant[operand]
  VM_EXPR_DUP,      //@alias dup @description a -> a a
  VM_EXPR_DROP,     //@alias drop @description a ->
  VM_EXPR_SWAP,     //@alias swap @description a b -> b a

  // binary arithmetic
  VM_EXPR_ADD,    //@alias + @description a b -> a + b
  VM_EXPR_SUB,    //@alias - @description a b -> a - b
  VM_EXPR_MUL,    //@alias * @description a b -> a * b
  VM_EXPR_DIV,    //@alias / @description a b -> a / b (b = 0 faults)
  VM_EXPR_MOD,    //@alias mod @description a b -> fmod(a, b) (b = 0 faults)
  VM_EXPR_POW,    //@alias pow @description a b -> a to the power b (a < 0 with fractional b faults)
  VM_EXPR_ROOT,   //@alias root @description a b -> b-th root of a (b = 0 faults; a < 0 needs an odd integer b)
  VM_EXPR_MIN,    //@alias min @description a b -> the smaller
  VM_EXPR_MAX,    //@alias max @description a b -> the larger
  VM_EXPR_ATAN2,  //@alias atan2 @description y x -> atan2(y, x), radians
  VM_EXPR_HYPOT,  //@alias hypot @description a b -> sqrt(a*a + b*b)

  // unary
  VM_EXPR_NEG,     //@alias neg @description a -> -a
  VM_EXPR_ABS,     //@alias abs @description a -> |a|
  VM_EXPR_SQRT,    //@alias sqrt @description a -> square root (a < 0 faults)
  VM_EXPR_SQUARE,  //@alias sq @description a -> a * a
  VM_EXPR_RECIP,   //@alias 1/x @description a -> 1 / a (a = 0 faults)
  VM_EXPR_FLOOR,   //@alias floor @description a -> round down
  VM_EXPR_CEIL,    //@alias ceil @description a -> round up
  VM_EXPR_ROUND,   //@alias round @description a -> round half away from zero
  VM_EXPR_TRUNC,   //@alias trunc @description a -> round toward zero
  VM_EXPR_SIGN,    //@alias sign @description a -> -1, 0 or 1
  VM_EXPR_SIN,     //@alias sin @description a -> sin(a), radians
  VM_EXPR_COS,     //@alias cos @description a -> cos(a), radians
  VM_EXPR_TAN,     //@alias tan @description a -> tan(a), radians
  VM_EXPR_ASIN,    //@alias asin @description a -> asin(a) (|a| > 1 faults)
  VM_EXPR_ACOS,    //@alias acos @description a -> acos(a) (|a| > 1 faults)
  VM_EXPR_ATAN,    //@alias atan @description a -> atan(a)
  VM_EXPR_EXP,     //@alias exp @description a -> e to the power a
  VM_EXPR_LOG,     //@alias ln @description a -> natural log (a <= 0 faults)
  VM_EXPR_LOG10,   //@alias log10 @description a -> base-10 log (a <= 0 faults)
  VM_EXPR_LOG2,    //@alias log2 @description a -> base-2 log (a <= 0 faults)
  VM_EXPR_DEG,     //@alias deg @description a -> radians to degrees
  VM_EXPR_RAD,     //@alias rad @description a -> degrees to radians

  // comparison (1 or 0)
  VM_EXPR_LT,  //@alias < @description a b -> a < b
  VM_EXPR_LE,  //@alias <= @description a b -> a <= b
  VM_EXPR_GT,  //@alias > @description a b -> a > b
  VM_EXPR_GE,  //@alias >= @description a b -> a >= b
  VM_EXPR_EQ,  //@alias == @description a b -> a == b (exact)
  VM_EXPR_NE,  //@alias != @description a b -> a != b (exact)

  // logic (non-zero is true; 1 or 0)
  VM_EXPR_AND,  //@alias and @description a b -> a and b
  VM_EXPR_OR,   //@alias or @description a b -> a or b
  VM_EXPR_XOR,  //@alias xor @description a b -> exactly one of a, b
  VM_EXPR_NOT,  //@alias not @description a -> not a

  VM_EXPR_SEL,  //@alias select @description c t f -> t if c is non-zero, else f
  VM_EXPR_OP_CNT
} vm_expr_op_e;

/* ==========================================================================
   Opcodes -- VM_BLK_EXPR_BIT (uint32)
   ========================================================================== */
//#ref-enum @alias Bit Expression Opcode
typedef enum vm_bit_op_e {
  VM_BIT_END = 0,  //@alias end @description Stop; the rest of the code is ignored.
  VM_BIT_IN,       //@alias in @description -> input[operand] (as u32)
  VM_BIT_K,        //@alias const @description -> constant[operand]
  VM_BIT_DUP,      //@alias dup @description a -> a a
  VM_BIT_DROP,     //@alias drop @description a ->
  VM_BIT_SWAP,     //@alias swap @description a b -> b a

  // binary
  VM_BIT_AND,  //@alias & @description a b -> a AND b
  VM_BIT_OR,   //@alias | @description a b -> a OR b
  VM_BIT_XOR,  //@alias ^ @description a b -> a XOR b
  VM_BIT_SHL,  //@alias << @description a n -> a shifted left by n (n >= 32 gives 0)
  VM_BIT_SHR,  //@alias >> @description a n -> a shifted right by n, zero fill (n >= 32 gives 0)
  VM_BIT_SAR,  //@alias >>> @description a n -> a shifted right by n, sign fill
  VM_BIT_ROL,  //@alias rol @description a n -> a rotated left by n mod 32
  VM_BIT_ROR,  //@alias ror @description a n -> a rotated right by n mod 32
  VM_BIT_GET,  //@alias bit @description a n -> bit n mod 32 of a (1 or 0)
  VM_BIT_SET,  //@alias set @description a n -> a with bit n mod 32 set
  VM_BIT_CLR,  //@alias clear @description a n -> a with bit n mod 32 cleared
  VM_BIT_TGL,  //@alias toggle @description a n -> a with bit n mod 32 flipped

  // unary
  VM_BIT_NOT,     //@alias ~ @description a -> every bit flipped
  VM_BIT_POPCNT,  //@alias popcount @description a -> number of set bits
  VM_BIT_CLZ,     //@alias clz @description a -> leading zero bits (32 for 0)
  VM_BIT_CTZ,     //@alias ctz @description a -> trailing zero bits (32 for 0)
  VM_BIT_BSWAP,   //@alias bswap @description a -> byte order reversed

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

//#vm-opcodes vm_expr_op_e
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

//#vm-opcodes vm_bit_op_e
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

/* ==========================================================================
   Load-time check
   ========================================================================== */

_Static_assert(VM_EXPR_END == 0 && VM_BIT_END == 0, "vm_expr_check() stops at opcode 0 in both sets");

static inline bool vm_expr_reject(vm_block_h b, uint16_t pc, uint8_t op, uint8_t reason) {
  VM_BLK_EMIT_ERR(ERR_VM_EXPR_BAD_CODE, .block_idx = b->cfg.block_idx, .pc = pc, .opcode = op, .reason = reason);
  return false;
}

/* Walks the code once with the opcode table: the code has no jumps, so one
   pass sees every stack depth it can reach. Reported with the pc and opcode of
   the first problem; the load then fails with ERR_VM_BLK_BAD_SHAPE. */
static inline bool vm_expr_check(vm_block_h b, const vm_expr_op_info_t* ops, uint8_t op_cnt) {
  if (b->cfg.custom_len < sizeof(vm_expr_code_t)) return vm_expr_reject(b, 0, 0, VM_EXPR_BAD_HEADER);
  const vm_expr_code_t* c = (const vm_expr_code_t*)vm_block_get_custom_data(b);
  if (vm_expr_size(c->const_cnt, c->code_len) > b->cfg.custom_len) return vm_expr_reject(b, 0, 0, VM_EXPR_BAD_HEADER);

  const uint8_t* code = vm_expr_bytecode(c);
  const vm_accessor_t** ins = vm_block_get_inputs(b);
  uint8_t sp = 0;
  uint16_t pc = 0;
  while (pc < c->code_len) {
    const uint16_t at = pc;
    const uint8_t op = code[pc++];
    if (op >= op_cnt) return vm_expr_reject(b, at, op, VM_EXPR_BAD_OPCODE);
    if (op == 0) break;  // END (0 in both opcode sets)
    const vm_expr_op_info_t info = ops[op];
    if (info.arg != VM_EXPR_ARG_NONE) {
      if (pc >= c->code_len) return vm_expr_reject(b, at, op, VM_EXPR_BAD_OPERAND);
      const uint8_t arg = code[pc++];
      const bool ok = (info.arg == VM_EXPR_ARG_INPUT) ? (arg < b->cfg.in_cnt && ins[arg]) : (arg < c->const_cnt);
      if (!ok) return vm_expr_reject(b, at, op, VM_EXPR_BAD_OPERAND);
    }
    if (sp < info.pops) return vm_expr_reject(b, at, op, VM_EXPR_BAD_UNDERFLOW);
    sp = (uint8_t)(sp - info.pops + info.pushes);
    if (sp > VM_EXPR_STACK_MAX) return vm_expr_reject(b, at, op, VM_EXPR_BAD_OVERFLOW);
  }
  if (sp != 1) return vm_expr_reject(b, pc, 0, VM_EXPR_BAD_RESULT);
  return true;
}

static inline bool vm_verify_expr(vm_block_h b) {
  return vm_expr_check(b, vm_expr_ops, VM_EXPR_OP_CNT);
}

static inline bool vm_verify_expr_bit(vm_block_h b) {
  return vm_expr_check(b, vm_bit_ops, VM_BIT_OP_CNT);
}

/* ==========================================================================
   Evaluator. Runs only code vm_expr_check() accepted: opcodes, operands and
   stack depth are not re-checked here; values are.
   ========================================================================== */

static inline bool vm_expr_fail(vm_block_h b, err_h e) {
  if (e) {
    vm_block_report_error(e, b);
  } else {
    vm_block_mark_failed(b);
  }
  return false;
}

static inline SE_MUST_USE err_h vm_expr_math_fault(vm_block_h b, vm_expr_code_t* c, uint16_t pc, uint8_t op, uint8_t reason) {
  if (c->rt & VM_EXPR_RT_FAULTED) return NULL;
  c->rt |= VM_EXPR_RT_FAULTED;
  return VM_BLK_ERR_NEW(ERR_VM_EXPR_MATH, .block_idx = b->cfg.block_idx, .pc = pc, .opcode = op, .reason = reason);
}

#define _MATH(reason_) return vm_expr_fail(b, vm_expr_math_fault(b, c, at, op, (reason_)))
#define _TRY(call_)                             \
  do {                                         \
    err_h e_ = (call_);                        \
    if (unlikely(e_)) return vm_expr_fail(b, e_); \
  } while (0)

#define _UN(expr_)                   \
  do {                               \
    __typeof__(st[0]) x = st[sp - 1]; \
    st[sp - 1] = (expr_);            \
  } while (0)

#define _BIN(expr_)                      \
  do {                                   \
    __typeof__(st[0]) y = st[--sp];      \
    __typeof__(st[0]) x = st[sp - 1];    \
    st[sp - 1] = (expr_);                \
  } while (0)

#define _CASE_STACK_OPS(END_, DUP_, DROP_, SWAP_)      \
  case END_:                                           \
    pc = c->code_len;                                  \
    break;                                             \
  case DUP_:                                           \
    st[sp] = st[sp - 1];                               \
    sp++;                                              \
    break;                                             \
  case DROP_:                                          \
    sp--;                                              \
    break;                                             \
  case SWAP_: {                                        \
    __typeof__(st[0]) t = st[sp - 1];                  \
    st[sp - 1] = st[sp - 2];                           \
    st[sp - 2] = t;                                    \
    break;                                             \
  }

static inline bool vm_expr_root_is_odd_int(float n) {
  float t = truncf(n);
  return n == t && fmodf(t, 2.0f) != 0.0f;
}

static inline float vm_expr_root_f(float x, float n) {
  return (x < 0.0f) ? -powf(-x, 1.0f / n) : powf(x, 1.0f / n);
}

static inline bool vm_expr_eval_f(vm_block_h b, vm_expr_code_t* c, float* out) {
  const uint8_t* code = vm_expr_bytecode(c);
  float st[VM_EXPR_STACK_MAX];
  float in[CONFIG_VM_BLOCK_MAX_IN];
  uint16_t loaded = 0;
  uint8_t sp = 0;
  uint16_t pc = 0;

  while (pc < c->code_len) {
    const uint16_t at = pc;
    const uint8_t op = code[pc++];

    switch (op) {
      _CASE_STACK_OPS(VM_EXPR_END, VM_EXPR_DUP, VM_EXPR_DROP, VM_EXPR_SWAP)

      case VM_EXPR_IN: {
        const uint8_t pin = code[pc++];
        const uint16_t mask = (uint16_t)(1u << pin);
        if (!(loaded & mask)) {
          const vm_accessor_t* acc = vm_block_get_inputs(b)[pin];
          if (likely((acc->flags & VM_ACC_F_CACHED) && (acc->c_payload.type == VM_OBJ_F))) {
            in[pin] = *(const float*)acc->c_payload.ptr;
          } else {
            _TRY(VM_OBJ_SCALAR_GET(in[pin], acc));
          }
          loaded |= mask;
        }
        st[sp++] = in[pin];
        break;
      }
      case VM_EXPR_K:
        st[sp++] = c->consts[code[pc++]].f;
        break;

      case VM_EXPR_ADD: _BIN(x + y); break;
      case VM_EXPR_SUB: _BIN(x - y); break;
      case VM_EXPR_MUL: _BIN(x * y); break;
      case VM_EXPR_DIV:
        if (unlikely(st[sp - 1] == 0.0f)) _MATH(VM_EXPR_MATH_DIV0);
        _BIN(x / y);
        break;
      case VM_EXPR_MOD:
        if (unlikely(st[sp - 1] == 0.0f)) _MATH(VM_EXPR_MATH_DIV0);
        _BIN(fmodf(x, y));
        break;
      case VM_EXPR_POW:
        if (unlikely(st[sp - 2] < 0.0f && st[sp - 1] != truncf(st[sp - 1]))) _MATH(VM_EXPR_MATH_DOMAIN);
        _BIN(powf(x, y));
        break;
      case VM_EXPR_ROOT:
        if (unlikely(st[sp - 1] == 0.0f)) _MATH(VM_EXPR_MATH_DIV0);
        if (unlikely(st[sp - 2] < 0.0f && !vm_expr_root_is_odd_int(st[sp - 1]))) _MATH(VM_EXPR_MATH_DOMAIN);
        _BIN(vm_expr_root_f(x, y));
        break;
      case VM_EXPR_MIN: _BIN(fminf(x, y)); break;
      case VM_EXPR_MAX: _BIN(fmaxf(x, y)); break;
      case VM_EXPR_ATAN2: _BIN(atan2f(x, y)); break;
      case VM_EXPR_HYPOT: _BIN(hypotf(x, y)); break;

      case VM_EXPR_NEG: _UN(-x); break;
      case VM_EXPR_ABS: _UN(fabsf(x)); break;
      case VM_EXPR_SQRT:
        if (unlikely(st[sp - 1] < 0.0f)) _MATH(VM_EXPR_MATH_DOMAIN);
        _UN(sqrtf(x));
        break;
      case VM_EXPR_SQUARE: _UN(x * x); break;
      case VM_EXPR_RECIP:
        if (unlikely(st[sp - 1] == 0.0f)) _MATH(VM_EXPR_MATH_DIV0);
        _UN(1.0f / x);
        break;
      case VM_EXPR_FLOOR: _UN(floorf(x)); break;
      case VM_EXPR_CEIL: _UN(ceilf(x)); break;
      case VM_EXPR_ROUND: _UN(roundf(x)); break;
      case VM_EXPR_TRUNC: _UN(truncf(x)); break;
      case VM_EXPR_SIGN: _UN(x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f)); break;
      case VM_EXPR_SIN: _UN(sinf(x)); break;
      case VM_EXPR_COS: _UN(cosf(x)); break;
      case VM_EXPR_TAN: _UN(tanf(x)); break;
      case VM_EXPR_ASIN:
        if (unlikely(fabsf(st[sp - 1]) > 1.0f)) _MATH(VM_EXPR_MATH_DOMAIN);
        _UN(asinf(x));
        break;
      case VM_EXPR_ACOS:
        if (unlikely(fabsf(st[sp - 1]) > 1.0f)) _MATH(VM_EXPR_MATH_DOMAIN);
        _UN(acosf(x));
        break;
      case VM_EXPR_ATAN: _UN(atanf(x)); break;
      case VM_EXPR_EXP: _UN(expf(x)); break;
      case VM_EXPR_LOG:
        if (unlikely(st[sp - 1] <= 0.0f)) _MATH(VM_EXPR_MATH_DOMAIN);
        _UN(logf(x));
        break;
      case VM_EXPR_LOG10:
        if (unlikely(st[sp - 1] <= 0.0f)) _MATH(VM_EXPR_MATH_DOMAIN);
        _UN(log10f(x));
        break;
      case VM_EXPR_LOG2:
        if (unlikely(st[sp - 1] <= 0.0f)) _MATH(VM_EXPR_MATH_DOMAIN);
        _UN(log2f(x));
        break;
      case VM_EXPR_DEG: _UN(x * (180.0f / (float)M_PI)); break;
      case VM_EXPR_RAD: _UN(x * ((float)M_PI / 180.0f)); break;

      case VM_EXPR_LT: _BIN(x < y ? 1.0f : 0.0f); break;
      case VM_EXPR_LE: _BIN(x <= y ? 1.0f : 0.0f); break;
      case VM_EXPR_GT: _BIN(x > y ? 1.0f : 0.0f); break;
      case VM_EXPR_GE: _BIN(x >= y ? 1.0f : 0.0f); break;
      case VM_EXPR_EQ: _BIN(x == y ? 1.0f : 0.0f); break;
      case VM_EXPR_NE: _BIN(x != y ? 1.0f : 0.0f); break;

      case VM_EXPR_AND: _BIN((x != 0.0f && y != 0.0f) ? 1.0f : 0.0f); break;
      case VM_EXPR_OR: _BIN((x != 0.0f || y != 0.0f) ? 1.0f : 0.0f); break;
      case VM_EXPR_XOR: _BIN(((x != 0.0f) != (y != 0.0f)) ? 1.0f : 0.0f); break;
      case VM_EXPR_NOT: _UN(x == 0.0f ? 1.0f : 0.0f); break;

      case VM_EXPR_SEL: {
        float f_arm = st[--sp];
        float t_arm = st[--sp];
        st[sp - 1] = (st[sp - 1] != 0.0f) ? t_arm : f_arm;
        break;
      }

      default:
        break;  // unreachable: vm_verify_expr accepted every opcode
    }
  }

  if (unlikely(!isfinite(st[0]))) {
    return vm_expr_fail(b, vm_expr_math_fault(b, c, c->code_len, VM_EXPR_END, VM_EXPR_MATH_NOT_FINITE));
  }

  *out = st[0];
  return true;
}

static inline uint32_t vm_expr_rotl32(uint32_t x, uint32_t n) {
  n &= 31u;
  return n ? ((x << n) | (x >> (32u - n))) : x;
}

static inline uint32_t vm_expr_rotr32(uint32_t x, uint32_t n) {
  n &= 31u;
  return n ? ((x >> n) | (x << (32u - n))) : x;
}

static inline bool vm_expr_eval_bit(vm_block_h b, vm_expr_code_t* c, uint32_t* out) {
  const uint8_t* code = vm_expr_bytecode(c);
  uint32_t st[VM_EXPR_STACK_MAX];
  uint32_t in[CONFIG_VM_BLOCK_MAX_IN];
  uint16_t loaded = 0;
  uint8_t sp = 0;
  uint16_t pc = 0;

  while (pc < c->code_len) {
    const uint8_t op = code[pc++];

    switch (op) {
      _CASE_STACK_OPS(VM_BIT_END, VM_BIT_DUP, VM_BIT_DROP, VM_BIT_SWAP)

      case VM_BIT_IN: {
        const uint8_t pin = code[pc++];
        const uint16_t mask = (uint16_t)(1u << pin);
        if (!(loaded & mask)) {
          const vm_accessor_t* acc = vm_block_get_inputs(b)[pin];
          if (likely((acc->flags & VM_ACC_F_CACHED) && (acc->c_payload.type == VM_OBJ_U32))) {
            in[pin] = *(const uint32_t*)acc->c_payload.ptr;
          } else {
            _TRY(VM_OBJ_SCALAR_GET(in[pin], acc));
          }
          loaded |= mask;
        }
        st[sp++] = in[pin];
        break;
      }
      case VM_BIT_K:
        st[sp++] = c->consts[code[pc++]].u;
        break;

      case VM_BIT_AND: _BIN(x & y); break;
      case VM_BIT_OR: _BIN(x | y); break;
      case VM_BIT_XOR: _BIN(x ^ y); break;
      case VM_BIT_SHL: _BIN(y >= 32u ? 0u : (x << y)); break;
      case VM_BIT_SHR: _BIN(y >= 32u ? 0u : (x >> y)); break;
      case VM_BIT_SAR: _BIN(y >= 32u ? (uint32_t)((int32_t)x >> 31) : (uint32_t)((int32_t)x >> y)); break;
      case VM_BIT_ROL: _BIN(vm_expr_rotl32(x, y)); break;
      case VM_BIT_ROR: _BIN(vm_expr_rotr32(x, y)); break;
      case VM_BIT_GET: _BIN((x >> (y & 31u)) & 1u); break;
      case VM_BIT_SET: _BIN(x | (1u << (y & 31u))); break;
      case VM_BIT_CLR: _BIN(x & ~(1u << (y & 31u))); break;
      case VM_BIT_TGL: _BIN(x ^ (1u << (y & 31u))); break;

      case VM_BIT_NOT: _UN(~x); break;
      case VM_BIT_POPCNT: _UN((uint32_t)__builtin_popcount(x)); break;
      case VM_BIT_CLZ: _UN(x ? (uint32_t)__builtin_clz(x) : 32u); break;
      case VM_BIT_CTZ: _UN(x ? (uint32_t)__builtin_ctz(x) : 32u); break;
      case VM_BIT_BSWAP: _UN(__builtin_bswap32(x)); break;

      default:
        break;  // unreachable: vm_verify_expr_bit accepted every opcode
    }
  }

  *out = st[0];
  return true;
}

#undef _MATH
#undef _TRY
#undef _UN
#undef _BIN
#undef _CASE_STACK_OPS

static inline void vm_blk_expr(vm_block_h b) {
  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_get_custom_data(b);

  IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b) {
    float r = 0.0f;
    if (unlikely(!vm_expr_eval_f(b, c, &r))) return;
    vm_obj_h q = vm_block_get_outputs(b)[0];
    BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(r, q, 0), b);

    c->rt &= (uint8_t)~VM_EXPR_RT_FAULTED;
    if (likely(!vm_block_failed(b))) vm_block_set_eno(b, true);
    return;
  }

  // case when error or non activated
  vm_block_set_eno(b, false);
}

static inline void vm_blk_expr_bit(vm_block_h b) {
  vm_expr_code_t* c = (vm_expr_code_t*)vm_block_get_custom_data(b);

  IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b) {
    uint32_t r = 0;
    if (unlikely(!vm_expr_eval_bit(b, c, &r))) return;
    vm_obj_h q = vm_block_get_outputs(b)[0];
    BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(r, q, 0), b);

    c->rt &= (uint8_t)~VM_EXPR_RT_FAULTED;
    if (likely(!vm_block_failed(b))) vm_block_set_eno(b, true);
    return;
  }

  // case when error or non activated
  vm_block_set_eno(b, false);
}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_EXPR @title Expression @category data @state vm_expr_code_t @state-tail consts u32[const_cnt], code u8[code_len] (opcodes vm_expr_op_e) @opcodes vm_expr_op_e @activation triggered Runs when an input it reads is fresh and the block is enabled.
//@block-description Evaluates an RPN float expression over its inputs when an input is fresh and the block is enabled; the result goes to output 0.
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
//@out 0 result @title Result @description The expression's value (float). @value f32
#define VM_BLOCK_TYPE_EXPR \
  {.run = vm_blk_expr, .check = vm_verify_expr, .min_in = 0, .min_q = 1, .required_in = 0x0u, .state_len = sizeof(vm_expr_code_t)}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_EXPR_BIT @title Bit Expression @category data @state vm_expr_code_t @state-tail consts u32[const_cnt], code u8[code_len] (opcodes vm_bit_op_e) @opcodes vm_bit_op_e @activation triggered Runs when an input it reads is fresh and the block is enabled.
//@block-description Evaluates an RPN uint32 bitwise expression over its inputs when an input is fresh and the block is enabled.
//@rule constants and code fit custom_len (4 + 4 x const_cnt + code_len bytes). @error ERR_VM_EXPR_BAD_CODE
//@rule Every opcode is a known value; code after END is ignored. @error ERR_VM_EXPR_BAD_CODE
//@rule An IN operand names a wired input (below in_cnt); a K operand a constant (below const_cnt). @error ERR_VM_EXPR_BAD_CODE
//@rule The stack never holds fewer values than an opcode takes, nor more than VM_EXPR_STACK_MAX. @error ERR_VM_EXPR_BAD_CODE
//@rule The code ends (END or its last byte) with exactly one value on the stack. @error ERR_VM_EXPR_BAD_CODE
//@example bit 3 of a @in 8 @consts 3 @code VM_BIT_IN 0 VM_BIT_K 0 VM_BIT_GET VM_BIT_END @result 1
//@example (a << 4) | b @in 10, 5 @consts 4 @code VM_BIT_IN 0 VM_BIT_K 0 VM_BIT_SHL VM_BIT_IN 1 VM_BIT_OR @result 165
//@example set bits in a @in 0xF0F0 @code VM_BIT_IN 0 VM_BIT_POPCNT @result 8
//@in * pin @title Input @description Read by the IN opcode; any number, only the ones the code names are read. @value u32
//@out 0 result @title Result @description The expression's value (u32). @value u32
#define VM_BLOCK_TYPE_EXPR_BIT \
  {.run = vm_blk_expr_bit, .check = vm_verify_expr_bit, .min_in = 0, .min_q = 1, .required_in = 0x0u, .state_len = sizeof(vm_expr_code_t)}
