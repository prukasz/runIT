#pragma once

#include "cmd.h"
#include "errors.h"
#include "router_errors.h"
#include "router_ids.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The commands of this board, not their frames: the tree of module routers and the two tasks that run them. Whoever
 * hands the router a command (the session, the board itself) gives the callback its answer goes to (router_done_f).
 * ROUTER.MD. The handler type, cmd_reply and ERR_CMD_* are cmd.h's; a command as an error names it (err_cmd_t) and
 * the error record are errors.h's. */

/* ----------------------------------------------------------------------------
 * A command's flags and an answer's status
 * ------------------------------------------------------------------------- */

/* A command's flags (err_cmd_t.flags): the one definition of them. The session's frame byte carries them at these
   bits (SYS_SESSION.MD "Frames"). */
//#ref-defines router_flag @alias Request Flags @prefix ROUTER_FLAG_
#define ROUTER_FLAG_URGENT  (1u << 0)  //@alias Urgent @description Runs on the urgent task, next to normal commands
#define ROUTER_FLAG_NOREPLY (1u << 1)  //@alias No reply @description No answer; a failure goes to the errors stream

/** @brief What an answer is: the session carries it in its frame byte. */
//#ref-enum @alias Response Status
typedef enum {
  ROUTER_STATUS_OK = 0,     //@alias OK @description The command succeeded; the answer's data follows
  ROUTER_STATUS_ERROR = 1,  //@alias Error @description The command failed; the answer's data is the error record
} router_status_e;

#define ROUTER_SRC_SELF 0xFEu  // err_cmd_t.src of the board's own commands (router_inject): SYS_SESSION_ID_SELF

/* ----------------------------------------------------------------------------
 * Command tables
 * ------------------------------------------------------------------------- */

/** @brief A root key and its module router. */
typedef struct {
  uint8_t key;
  cmd_f fn;
} router_entry_t;

/** @brief One command of a module: its code and its two sides (NULL = the command has no such side). */
typedef struct {
  uint8_t code;
  cmd_f read;
  cmd_f write;
} router_op_t;

// The rows of a module's command table.
#define ROUTER_R(code, read)         {(code), (read), NULL}
#define ROUTER_W(code, write)        {(code), NULL, (write)}
#define ROUTER_RW(code, read, write) {(code), (read), (write)}

/**
 * @brief Calls the entry of `table` whose key is in.data[0], with the same `ctx`, and wraps a failure in
 * ERR_ROUTER_KEY{key}.
 */
MUST_USE err_h router_call(const router_entry_t* table, uint32_t count, void* ctx, cmd_in_t in, cmd_out_t* out);

/**
 * @brief Runs one command of a module: `in` is [dir][code][args]. The side `dir` asks for gets [code][args] (so the
 * op macros below parse it). A failure is wrapped in ERR_ROUTER_KEY{code}.
 * @return ERR_ROUTER_UNKNOWN_KEY for an unknown code, ERR_CMD_DIRECTION for a side the command doesn't have.
 */
MUST_USE err_h router_call_op(const router_op_t* table, uint32_t count, void* ctx, cmd_in_t in, cmd_out_t* out);

/**
 * @brief Copies the fixed-size payload after the key (in.data[1..]) into `args`; size 0 (args may be NULL) only checks
 * that nothing follows the key.
 * @return ERR_CMD_BAD_LEN unless exactly `size` bytes follow the key.
 */
MUST_USE err_h router_args(cmd_in_t in, void* args, uint32_t size);

/**
 * @brief Like router_args for a fixed header and a variable tail: copies the header and points `tail` at the rest.
 * Size 0 (hdr may be NULL): the whole payload is the tail.
 * @return ERR_CMD_BAD_LEN unless the header and at least one tail byte follow the key.
 */
MUST_USE err_h router_args_tail(cmd_in_t in, void* hdr, uint32_t size, cmd_in_t* tail);

/* ----------------------------------------------------------------------------
 * Op macros (ROUTER.MD "Routing"). They write the cmd_f, check and parse the
 * payload; the block after them is the body, which sees `ctx`, `args`,
 * `tail` (ROUTER_OP_TAIL) and `out`.
 * ------------------------------------------------------------------------- */

// Op with a fixed-size payload `args_t` (packed struct).
#define ROUTER_OP(fn, args_t)                                                                               \
  static err_h fn##_body(void* ctx, const args_t* args, cmd_out_t* out);                                    \
  static err_h fn(CMD_F_PARAMS) {                                                                           \
    args_t args;                                                                                            \
    ERR_TRY(router_args(in, &args, sizeof(args)));                                                          \
    return fn##_body(ctx, &args, out);                                                                      \
  }                                                                                                         \
  static err_h fn##_body(void* ctx, const args_t* args, cmd_out_t* out)

// Op without payload.
#define ROUTER_OP_NOARGS(fn)                                                                                \
  static err_h fn##_body(void* ctx, cmd_out_t* out);                                                        \
  static err_h fn(CMD_F_PARAMS) {                                                                           \
    ERR_TRY(router_args(in, NULL, 0));                                                                      \
    return fn##_body(ctx, out);                                                                             \
  }                                                                                                         \
  static err_h fn##_body(void* ctx, cmd_out_t* out)

// Op with a fixed header `hdr_t` and a variable tail (name, data).
#define ROUTER_OP_TAIL(fn, hdr_t)                                                                           \
  static err_h fn##_body(void* ctx, const hdr_t* args, cmd_in_t tail, cmd_out_t* out);                      \
  static err_h fn(CMD_F_PARAMS) {                                                                           \
    hdr_t args;                                                                                             \
    cmd_in_t tail;                                                                                          \
    ERR_TRY(router_args_tail(in, &args, sizeof(args), &tail));                                              \
    return fn##_body(ctx, &args, tail, out);                                                                \
  }                                                                                                         \
  static err_h fn##_body(void* ctx, const hdr_t* args, cmd_in_t tail, cmd_out_t* out)

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

/** @brief Binds a root key (router_ids.h) to a module router (called with ctx NULL). Boot only, before router_init. */
MUST_USE err_h router_register(uint8_t key, cmd_f fn);

/**
 * @brief Routes one command (`in` starts at the root key) on the calling task: the router tasks, and the replay of
 * stored frames (actions, project). Urgent and normal commands run at the same time, so module routers must be
 * thread-safe.
 */
MUST_USE err_h router_route(cmd_in_t in, cmd_out_t* out);

/** @brief Starts the urgent and work tasks. Module routers are registered before. */
MUST_USE err_h router_init(void);

/* ----------------------------------------------------------------------------
 * Commands in, answers out: every command comes with the callback its answer goes to, so the router keeps nothing of
 * who sent it.
 * ------------------------------------------------------------------------- */

/**
 * @brief Where a command's answer goes (the session's framer, a module that wants the answer). On the router task that ran
 * the command; never for a NOREPLY one. It may wait for room on the way back, at most its own bound.
 * @param ctx     The submitter's.
 * @param cmd     The command answered (its src, seq, flags, request).
 * @param status  router_status_e.
 * @param data    The answer (OK) or the error record (ERROR), in the task's own buffer: the CMD_HEAD bytes in front of
 *                it are the callback's to write (a frame's header), so the answer is framed without a copy.
 * @param len     Bytes in `data`, at most CMD_DATA_MAX.
 * @return NULL; else an error the router reports with the command (the answer is lost: a reader retries).
 */
typedef MUST_USE err_h (*router_done_f)(void* ctx, const err_cmd_t* cmd, uint8_t status, uint8_t* data,
                                        uint32_t len);

/**
 * @brief Queues one command: `cmd` and its request are copied, so both may go after the call. Never blocks; any task
 * but an ISR, a provider driver's RX task too.
 * @param cmd   The origin (src, seq, flags) and the request ([key][dir][...], 2..CMD_DATA_MAX bytes).
 * @param done  Where its answer goes; NULL: nowhere (a failure is reported only).
 * @param ctx   Given to `done`.
 * @return NULL when queued; ERR_ROUTER_FRAME_LEN, ERR_ROUTER_QUEUE_FULL: not queued, the caller answers it.
 */
MUST_USE err_h router_submit(const err_cmd_t* cmd, router_done_f done, void* ctx);

/**
 * @brief Queues a command of the board itself: router_submit with src ROUTER_SRC_SELF and seq from the router's own
 * counter (the actions, the project's start, a call of the board to itself).
 * @param req    [key][dir][...].
 * @param flags  ROUTER_FLAG_URGENT, ROUTER_FLAG_NOREPLY.
 * @param done   Where its answer goes; NULL: nowhere.
 * @param ctx    Given to `done`.
 * @return NULL when queued; ERR_ROUTER_FRAME_LEN, ERR_ROUTER_QUEUE_FULL.
 */
MUST_USE err_h router_inject(cmd_in_t req, uint8_t flags, router_done_f done, void* ctx);

/**
 * @brief Ends a command's chain: logged with the command ("cmd src f0 seq 7 flags 00 req 02 02 00") and handed to the
 * error sink with it (its record on the errors stream). For a command the router refused, by whoever answers it.
 */
void router_report_cmd(const err_cmd_t* cmd, err_h err);

/* ----------------------------------------------------------------------------
 * Tap, origin
 * ------------------------------------------------------------------------- */

/** Sees every command the tasks route (the board's own too), on the routing task. */
typedef struct {
  /** Before routing. true = taken: answered OK with no data and not routed. */
  bool (*take)(const err_cmd_t* cmd);
  /** After routing a command that wasn't taken. `result` is borrowed. */
  void (*seen)(const err_cmd_t* cmd, err_h result);
} router_tap_t;

/** @brief Sets the tap (one; NULL removes it). Boot only. */
void router_set_tap(const router_tap_t* tap);

/**
 * @brief The command the calling task is routing, valid until its handler returns. Inside a replay: the outer one.
 * @return The origin; NULL outside the router tasks.
 */
const err_cmd_t* router_origin(void);

/** @brief True when the caller runs on a router task (urgent or work): what waits for an answer must not run there. */
bool router_is_task(void);

/* ----------------------------------------------------------------------------
 * Urgent work: what must not wait behind a command (the safe state of sys_system)
 * ------------------------------------------------------------------------- */

/**
 * @brief Runs `fn` on the urgent task, soon, once: called again before it ran, it still runs once. Any task or a
 * timer callback, not an ISR. `fn` may block on I/O.
 * @return NULL when queued; ERR_ROUTER_BUSY (another fn waits to run).
 */
MUST_USE err_h router_run_urgent(void (*fn)(void));

#ifdef __cplusplus
}
#endif
