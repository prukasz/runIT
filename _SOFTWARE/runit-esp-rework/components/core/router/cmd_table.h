#pragma once

#include <string.h>  // memcpy in CMD_HANDLER

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file cmd_table.h
 * @brief A table of ops, the one format for every set of commands: a device contract (sys_device) and a module's router
 * (router) alike. The ops are written once, as P_OPERATIONS(X, P) of rows X(P, NAME, n, key_t, value_t, flags): the
 * read takes key_t and answers value_t, the write takes value_t (the mirror; cmd_none_t for a side with nothing).
 * CMD_TABLE_DECLARE(P) gives P_OP_<NAME> = n and the types P_key_<NAME>_t, P_value_<NAME>_t, so a handler, its answer
 * and a caller's frame all come from the table and the compiler checks them. `flags` are the user's (the registry's
 * SYS_DEVICE_OP_*); a router's are 0.
 * @usage #define PWRM_OPERATIONS(X, P)                                    \
 *          X(P, PSU,    0x03, cmd_none_t, sys_power_psu_t,    0)         \
 *          X(P, STATUS, 0x07, cmd_none_t, sys_power_status_t, 0)
 *        CMD_TABLE_DECLARE(PWRM)  // PWRM_OP_PSU = 0x03, PWRM_value_PSU_t = sys_power_psu_t, ...
 */

/** @brief A key or value of nothing (an op without args, a read without a key). */
typedef struct __attribute__((packed)) {
} cmd_none_t;

#define CMD__TABLE_CODE(P, name, n, key_t, value_t, flags) P##_OP_##name = (n),
#define CMD__TABLE_TYPES(P, name, n, key_t, value_t, flags) \
  typedef key_t P##_key_##name##_t;                         \
  typedef value_t P##_value_##name##_t;

/** @brief The codes and types of table P_OPERATIONS. */
#define CMD_TABLE_DECLARE(P)                   \
  enum { P##_OPERATIONS(CMD__TABLE_CODE, P) }; \
  P##_OPERATIONS(CMD__TABLE_TYPES, P)

/**
 * @brief The answer of a read handler of op NAME of table P: its value_t built from the fields given (or the value
 * itself, for a scalar), written to the handler's `out`.
 * @usage return CMD_REPLY(PWRM, STATUS, .input_mV = mv, .limit_mW = limit);
 */
#define CMD_REPLY(P, name, ...) \
  cmd_reply(out, &(const P##_value_##name##_t){__VA_ARGS__}, sizeof(P##_value_##name##_t))

/** @brief On a parameter of a handler's body a macro writes: the body need not use it. */
#define CMD_UNUSED __attribute__((unused))

/**
 * @brief Writes a cmd_f whose payload is exactly one args_t (cmd_none_t: nothing), the handler of every fixed frame: a
 * module's op (ROUTER_READ / ROUTER_WRITE) and a device's (OP_READ / OP_WRITE). `in` is the payload only. The body that
 * follows sees `c` (the handler's ctx as ctx_t*; void for none), `args` (a copy, aligned) and `out`, and need not use
 * any of them. ERR_CMD_BAD_LEN{got, need} for a payload of another size.
 * @usage CMD_HANDLER(get_level, pca_t, io_pin_args_t) { return reply_level(c, args->pin, out); }
 */
#define CMD_HANDLER(fn, ctx_t, args_t)                                                                            \
  static err_h fn##_body(ctx_t* c, const args_t* args, cmd_out_t* out);                                           \
  static err_h fn(CMD_F_PARAMS) {                                                                                 \
    args_t args;                                                                                                  \
    ERR_CHECK(in.len == sizeof(args), ERR_CMD_BAD_LEN, .got = (uint16_t)in.len, .need = (uint16_t)sizeof(args));  \
    memcpy(&args, in.data, sizeof(args));                                                                         \
    return fn##_body(ctx, &args, out);                                                                            \
  }                                                                                                               \
  static err_h fn##_body(ctx_t* c CMD_UNUSED, const args_t* args CMD_UNUSED, cmd_out_t* out CMD_UNUSED)

/**
 * @brief Writes a cmd_f whose payload is a head of a fixed size and then bytes of any number (the tail): the one
 * handler for every payload that is not a fixed size. `in` is the payload only. The body that follows sees `c` (the
 * handler's ctx as ctx_t*; void for none), `args` (the head, used in place: head_t is packed), `tail` and `tail_len`
 * (the bytes after it, 0 for none) and out. ERR_CMD_BAD_LEN for a payload shorter than the head; what the tail must be
 * (not empty, as long as a field of the head says) is the body's to check.
 * @usage CMD_HANDLER_TAIL(raw_write, bus_t, i2c_raw_t) { return reg_write(c, args->addr, args->reg, tail, tail_len); }
 */
#define CMD_HANDLER_TAIL(fn, ctx_t, head_t)                                                                        \
  _Static_assert(_Alignof(head_t) == 1, #head_t ": the head of a payload with a tail is packed");                  \
  static err_h fn##_body(ctx_t* c, const head_t* args, const uint8_t* tail, uint32_t tail_len, cmd_out_t* out);    \
  static err_h fn(CMD_F_PARAMS) {                                                                                  \
    ERR_CHECK(in.len >= sizeof(head_t), ERR_CMD_BAD_LEN, .got = (uint16_t)in.len, .need = (uint16_t)sizeof(head_t)); \
    return fn##_body(ctx, (const head_t*)in.data, in.data + sizeof(head_t), in.len - (uint32_t)sizeof(head_t), out); \
  }                                                                                                                \
  static err_h fn##_body(ctx_t* c CMD_UNUSED, const head_t* args CMD_UNUSED, const uint8_t* tail CMD_UNUSED,       \
                         uint32_t tail_len CMD_UNUSED, cmd_out_t* out CMD_UNUSED)

#ifdef __cplusplus
}
#endif
