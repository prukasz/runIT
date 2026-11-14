#pragma once

#include "cmd.h"
#include "router_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file router.h
 * @brief Module routing and root dispatch (ROUTER.MD), with no queue and no tasks: the op tables, the tree of
 * module routers and the root table. Builds on cmd.h; a module router compiles without the queue (router_queue.h).
 * @usage ROUTER_MODULE(my_router, k_ops);
 */

/* ============================================================================
 * Module routing (per-module op tables)
 * ========================================================================= */

/** @brief One command of a module: its code and its two sides (NULL = the command has no such side). The rows come
 * from the module's op table (ROUTER_ENTRY*, router_table.h). */
typedef struct {
  uint8_t code;
  cmd_f read;
  cmd_f write;
} router_op_t;

/**
 * @brief Runs one command of a module: `in` is [dir][code][args]. The side `dir` asks for gets the args only (the
 * payload: the handlers of router_table.h parse it). A failure is wrapped in ERR_ROUTER_OP{code}.
 * @param[in]  table The module's op table
 * @param[in]  count Rows in `table`
 * @param[in]  ctx   Passed to the op
 * @param[in]  in    [dir][code][args]
 * @param[out] out   The answer
 * @return ERR_ROUTER_NO_KEY without [dir][code], ERR_ROUTER_UNKNOWN_OP for an unknown code, ERR_CMD_DIRECTION for a
 * side the command doesn't have.
 */
MUST_USE err_h router_call_op(const router_op_t* table, uint32_t count, void* ctx, cmd_in_t in, cmd_out_t* out);

// Declares a module router in its header.
#define ROUTER_MODULE_DECLARE(fn) MUST_USE err_h fn(CMD_F_PARAMS)

// Defines the module router `fn`: consumes its own key and runs the command of `table` (router_call_op). Prefix
// `static` for a file-local one.
#define ROUTER_MODULE(fn, table)                                                                                       \
  err_h fn(CMD_F_PARAMS) {                                                                                             \
    return router_call_op(table, sizeof(table) / sizeof((table)[0]), ctx, (cmd_in_t){in.data + 1, in.len - 1}, out);   \
  }

/* ============================================================================
 * Root table and dispatch
 * ========================================================================= */

/** @brief A root key and its module router. */
typedef struct {
  uint8_t key;
  cmd_f fn;
} router_entry_t;

/**
 * @brief Calls the entry of `table` whose key is in.data[0], with the same `ctx`, and wraps a failure in
 * ERR_ROUTER_KEY{key}.
 * @param[in]  table The module's command table
 * @param[in]  count Rows in `table`
 * @param[in]  ctx   Passed to the entry
 * @param[in]  in    The command, starting at the key
 * @param[out] out   The answer
 * @return ERR_ROUTER_NO_KEY, ERR_ROUTER_UNKNOWN_KEY, or the entry's error wrapped in ERR_ROUTER_KEY
 */
MUST_USE err_h router_call(const router_entry_t* table, uint32_t count, void* ctx, cmd_in_t in, cmd_out_t* out);

/**
 * @brief Queues a module router for router_root_init() to bind (used by ROUTER_MODULE_REGISTER before app_main).
 * Nothing is checked and no error is made here; router_root_init() binds each entry into the root table and
 * returns any error.
 * @param[in] key A root key (MODULE_ID_<NAME>)
 * @param[in] fn  The module router
 */
void router_register(uint8_t key, cmd_f fn);

/* The module registers its router itself, before app_main: no other component names it. The root key is the
 * module's number (MODULE_ID_<NAME>, module_id.h), which no other module has; the module's component is linked as
 * WHOLE_ARCHIVE (nothing references it). */
#define ROUTER__REGISTER(tag, id, fn)                                \
  __attribute__((constructor)) static void router__reg_##tag(void) { \
    router_register((uint8_t)(id), (fn));                            \
  }

/**
 * @brief Registers the module router `fn` under the number of module NAME (MODULE_ID_<NAME>) by itself.
 * @usage ROUTER_MODULE_REGISTER(SYS_DEVICE, sys_device_router)
 */
#define ROUTER_MODULE_REGISTER(name, fn) ROUTER__REGISTER(name, MODULE_ID_##name, fn)

/**
 * @brief Binds every module router queued via ROUTER_MODULE_REGISTER / router_register() into the root table. Boot,
 * once, called by router_init (router_queue.h).
 * @return ERR_CMD_BAD_ARG, ERR_ROUTER_KEY_TAKEN, or ERR_ROUTER_TABLE_FULL
 */
MUST_USE err_h router_root_init(void);

/**
 * @brief Routes one command (`in` starts at the root key) on the calling task: the router tasks, and the replay of
 * stored frames (actions, project). Urgent and normal commands run at the same time, so module routers must be
 * thread-safe.
 * @param[in]  in  The command, starting at the root key
 * @param[out] out The answer
 * @return ERR_ROUTER_NO_KEY, ERR_ROUTER_UNKNOWN_KEY, or the module's error
 */
MUST_USE err_h router_route(cmd_in_t in, cmd_out_t* out);

#ifdef __cplusplus
}
#endif
