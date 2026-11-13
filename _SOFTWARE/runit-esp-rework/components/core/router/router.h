#pragma once

#include "errors.h"
#include "router_errors.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file router.h
 * @brief How a command is handled (ROUTER.MD), with no queue and no tasks: the handler, the direction, the answer and
 * the frame size every limit comes from, the op tables, the tree of module routers and the root table. A device
 * registry, a contract and a module router build on it alone, so a device compiles without the queue (router_queue.h).
 * @usage static err_h my_read(void* ctx, cmd_in_t in, cmd_out_t* out) { return cmd_reply(out, &v, sizeof(v)); }
 */

/* The frame size (ROUTER.MD "Frame size"): one for every link, buffer and stored frame, the same on the board and the
   client, so nothing is sized per link. */
//#ref-defines cmd_frame @alias Frame size @prefix CMD_
#define CMD_FRAME_MAX 512u  //@alias Largest frame @description Bytes of a whole frame, its header included
#define CMD_HEAD      5u    //@alias Frame header @description Bytes a frame keeps for its header in front of its data

// What a frame carries after its header: a request's command, an answer's data, a PUSH frame's [stream id or channel][payload].
#define CMD_DATA_MAX (CMD_FRAME_MAX - CMD_HEAD)

/** @brief A handler's input: the payload only (an op's args). The router strips [dir][code], the device registry the
 * op; a module router (a root entry) gets its whole command, its key first. */
typedef struct {
  const uint8_t* data; 
  uint32_t len;
} cmd_in_t;

/** @brief A handler's answer buffer. */
typedef struct {
  uint8_t* data;
  uint32_t cap;  /**< What the answer may hold: what cmd_reply() fills at most; longer data is paged by its command
                      (ROUTER.MD "The handler") */
  uint32_t len;  /**< Written by the handler; 0 = empty answer */
} cmd_out_t;

/**
 * @brief A command handler, the same for the router's modules and the devices' ops.
 * @param ctx  NULL from the router root; the device's struct from the registry.
 * @param in   The input.
 * @param out  The answer.
 * @return NULL on success, otherwise the error chain.
 */
typedef MUST_USE err_h (*cmd_f)(void* ctx, cmd_in_t in, cmd_out_t* out);

#define CMD_F_PARAMS void *ctx, cmd_in_t in, cmd_out_t *out  // for the macros that write a cmd_f

/**
 * @brief Which side of a command a request runs: the byte after the root key of every request (ROUTER.MD "Wire
 * format"). A command has a read side, a write side or both; with both, the read answers what the write takes (the
 * mirror, ROUTER.MD). A write answers no data unless its command says otherwise.
 */
//#ref-enum @alias Command Direction
typedef enum {
  CMD_DIR_READ = 0,   //@alias Read @description Asks for a value; changes nothing
  CMD_DIR_WRITE = 1,  //@alias Write @description Sets a value or runs an action
} cmd_dir_e;

/**
 * @brief The side of a command that `dir` asks for (NULL `read` / `write`: the command has none).
 * @param[in]  read  The read side, or NULL
 * @param[in]  write The write side, or NULL
 * @param[in]  dir   cmd_dir_e
 * @param[out] fn    The side asked for
 * @return NULL when found; ERR_CMD_DIRECTION for a missing side or a dir that is neither read nor write.
 */
MUST_USE err_h cmd_side(cmd_f read, cmd_f write, uint8_t dir, cmd_f* fn);

/**
 * @brief Writes the whole answer.
 * @param[out] out  The answer
 * @param[in]  data The bytes
 * @param[in]  len  Number of bytes
 * @return NULL when written; ERR_CMD_OUT_FULL (more than out->cap).
 * @usage return cmd_reply(out, &value, sizeof(value));
 */
MUST_USE err_h cmd_reply(cmd_out_t* out, const void* data, uint32_t len);

/**
 * @brief Appends to the answer (out->len grows): an answer built in pieces.
 * @param[in,out] out  The answer
 * @param[in]     data The bytes
 * @param[in]     len  Number of bytes
 * @return NULL when appended; ERR_CMD_OUT_FULL.
 * @usage ERR_TRY(cmd_append(out, &head, sizeof(head))); ERR_TRY(cmd_append(out, name, name_len));
 */
MUST_USE err_h cmd_append(cmd_out_t* out, const void* data, uint32_t len);


/* ----------------------------------------------------------------------------
 * Command tables
 * ------------------------------------------------------------------------- */

/** @brief A root key and its module router. */
typedef struct {
  uint8_t key;
  cmd_f fn;
} router_entry_t;

/** @brief One command of a module: its code and its two sides (NULL = the command has no such side). The rows come
 * from the module's op table (ROUTER_ENTRY*, router_table.h). */
typedef struct {
  uint8_t code;
  cmd_f read;
  cmd_f write;
} router_op_t;

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

/* ----------------------------------------------------------------------------
 * Module routers and the root table
 * ------------------------------------------------------------------------- */

// Declares a module router in its header.
#define ROUTER_MODULE_DECLARE(fn) MUST_USE err_h fn(CMD_F_PARAMS)

// Defines the module router `fn`: consumes its own key and runs the command of `table` (router_call_op). Prefix
// `static` for a file-local one.
#define ROUTER_MODULE(fn, table)                                                                            \
  err_h fn(CMD_F_PARAMS) {                                                                                  \
    return router_call_op(table, ARRAY_LEN(table), ctx, (cmd_in_t){in.data + 1, in.len - 1}, out);          \
  }

/**
 * @brief Binds a root key (a module's number, module_id.h) to a module router (called with ctx NULL). Boot only, before router_register_pending.
 * @param[in] key A root key
 * @param[in] fn  The module router
 * @return The error when the key is taken or out of range
 */
MUST_USE err_h router_register(uint8_t key, cmd_f fn);

/**
 * @brief Keeps a module router for router_register_pending to register (ROUTER_MODULE_REGISTER, a constructor before
 * app_main): nothing is checked and no error is made here, router_register_pending registers it with router_register and
 * returns its error.
 * @param[in] key A root key
 * @param[in] fn  The module router
 */
void router_register_later(uint8_t key, cmd_f fn);

/* The module registers its router itself, before app_main: no other component names it. The root key is the
 * module's number (MODULE_ID_<NAME>, module_id.h), which no other module has; the module's component is linked as
 * WHOLE_ARCHIVE (nothing references it). */
#define ROUTER__REGISTER(id, fn)                                     \
  __attribute__((constructor)) static void s_router_register(void) { \
    router_register_later((uint8_t)(id), (fn));                      \
  }

/**
 * @brief Registers the module router `fn` under the number of module NAME (MODULE_ID_<NAME>) by itself. One per file.
 * @usage ROUTER_MODULE_REGISTER(SYS_DEVICE, sys_device_router)
 */
#define ROUTER_MODULE_REGISTER(name, fn) ROUTER__REGISTER(MODULE_ID_##name, fn)

/**
 * @brief Routes one command (`in` starts at the root key) on the calling task: the router tasks, and the replay of
 * stored frames (actions, project). Urgent and normal commands run at the same time, so module routers must be
 * thread-safe.
 * @param[in]  in  The command, starting at the root key
 * @param[out] out The answer
 * @return ERR_ROUTER_NO_KEY, ERR_ROUTER_UNKNOWN_KEY, or the module's error
 */
MUST_USE err_h router_route(cmd_in_t in, cmd_out_t* out);

/**
 * @brief Registers the module routers that registered themselves (ROUTER_MODULE_REGISTER) with router_register. Boot,
 * once, after the app's own router_register calls: router_init (router_queue.h) calls it.
 * @return ERR_ROUTER_KEY_TAKEN / ERR_ROUTER_TABLE_FULL of a self-registered router
 */
MUST_USE err_h router_register_pending(void);

#ifdef __cplusplus
}
#endif
