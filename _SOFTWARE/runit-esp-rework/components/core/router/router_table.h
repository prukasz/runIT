#pragma once

#include "cmd_table.h"
#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file router_table.h
 * @brief A module's router written from its op table (cmd_table.h), the one way to write a module's commands: the
 * handlers typed by the table, a getter or setter of the module bound in one line, the rows of router_op_t from the
 * names. The op's code is the table's n. A handler's `in` is the payload only (router_call_op strips [dir][code]).
 * @usage #define PWRM_OPERATIONS(X, P) X(P, PSU, 0x03, cmd_none_t, sys_power_psu_t, 0) ...
 *        CMD_TABLE_DECLARE(PWRM)
 *        ROUTER_GETTER(PWRM, PSU, sys_power_manager_get_psu)
 *        ROUTER_SETTER(PWRM, PSU, sys_power_manager_set_psu)
 *        ROUTER_READ(PWRM, MATRIX) { ... }  // a handler with logic of its own
 *        static const router_op_t k_ops[] = {ROUTER_ENTRY(PWRM, PSU), ROUTER_ENTRY_R(PWRM, MATRIX), ...};
 *        ROUTER_MODULE(sys_power_manager_router, k_ops)
 */

/** @brief The read handler of op NAME: its body sees `args` (the key, P_key_NAME_t) and `out` (and `c`, the ctx: NULL
 * from the root). */
#define ROUTER_READ(P, name) CMD_HANDLER(P##_r_##name, void, P##_key_##name##_t)

/** @brief The write handler of op NAME: its body sees `args` (the value, P_value_NAME_t) and `out`. */
#define ROUTER_WRITE(P, name) CMD_HANDLER(P##_w_##name, void, P##_value_##name##_t)

/**
 * @brief The write handler of op NAME whose payload is its value (the head, packed) and then bytes of any number: a
 * name, data. Its body sees `args` (the head, in place), `tail`, `tail_len` and `out` (CMD_HANDLER_TAIL). What the
 * tail must be (not empty, as long as a field of the head says) is the body's to check.
 * @usage ROUTER_WRITE_TAIL(NVS, APPEND) { return nvs_append(args->file, tail, tail_len); }
 */
#define ROUTER_WRITE_TAIL(P, name) CMD_HANDLER_TAIL(P##_w_##name, void, P##_value_##name##_t)

/**
 * @brief The read of op NAME as the module's getter `err_h get(P_value_NAME_t* out)`: no key, the value answered.
 * @usage ROUTER_GETTER(PWRM, STATUS, sys_power_manager_get_status)
 */
#define ROUTER_GETTER(P, name, get)                 \
  ROUTER_READ(P, name) {                            \
    P##_value_##name##_t value_;                    \
    ERR_TRY(get(&value_));                          \
    return cmd_reply(out, &value_, sizeof(value_)); \
  }

/**
 * @brief The write of op NAME as the module's setter `err_h set(const P_value_NAME_t* value)`.
 * @usage ROUTER_SETTER(PWRM, PSU, sys_power_manager_set_psu)
 */
#define ROUTER_SETTER(P, name, set) \
  ROUTER_WRITE(P, name) {           \
    return set(args);               \
  }

/* The row of op NAME in the module's router_op_t table: both sides, the read or the write. */
#define ROUTER_ENTRY(P, name)   {P##_OP_##name, P##_r_##name, P##_w_##name}
#define ROUTER_ENTRY_R(P, name) {P##_OP_##name, P##_r_##name, NULL}
#define ROUTER_ENTRY_W(P, name) {P##_OP_##name, NULL, P##_w_##name}

#ifdef __cplusplus
}
#endif
