#include "vm_expr_core.h"

/* ==========================================================================
   Load-time check
   ========================================================================== */

_Static_assert(VM_EXPR_END == 0 && VM_BIT_END == 0, "vm_expr_check() stops at opcode 0 in both sets");

static bool vm_expr_reject(vm_block_h b, uint16_t pc, uint8_t op, uint8_t reason) {
  VM_BLK_EMIT_ERR(ERR_VM_EXPR_BAD_CODE, .block_idx = b->cfg.block_idx, .pc = pc, .opcode = op, .reason = reason);
  return false;
}

/* Walks the code once with the opcode table: the code has no jumps, so one
   pass sees every stack depth it can reach. Reported with the pc and opcode of
   the first problem; the load then fails with ERR_VM_BLK_BAD_SHAPE. */
bool vm_expr_check(vm_block_h b, const vm_expr_op_info_t* ops, uint8_t op_cnt) {
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

/* ==========================================================================
   Evaluator. Runs only code vm_expr_check() accepted: opcodes, operands and
   stack depth are not re-checked here; values are.
   ========================================================================== */

static bool vm_expr_fail(vm_block_h b, err_h e) {
  if (e) {
    vm_block_report_error(e, b);
  } else {
    vm_block_mark_failed(b);
  }
  return false;
}

static SE_MUST_USE err_h vm_expr_math_fault(vm_block_h b, vm_expr_code_t* c, uint16_t pc, uint8_t op, uint8_t reason) {
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

static bool vm_expr_root_is_odd_int(float n) {
  float t = truncf(n);
  return n == t && fmodf(t, 2.0f) != 0.0f;
}

static float vm_expr_root_f(float x, float n) {
  return (x < 0.0f) ? -powf(-x, 1.0f / n) : powf(x, 1.0f / n);
}

bool vm_expr_eval_f(vm_block_h b, vm_expr_code_t* c, float* out) {
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

static uint32_t vm_expr_rotl32(uint32_t x, uint32_t n) {
  n &= 31u;
  return n ? ((x << n) | (x >> (32u - n))) : x;
}

static uint32_t vm_expr_rotr32(uint32_t x, uint32_t n) {
  n &= 31u;
  return n ? ((x >> n) | (x << (32u - n))) : x;
}

bool vm_expr_eval_bit(vm_block_h b, vm_expr_code_t* c, uint32_t* out) {
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
