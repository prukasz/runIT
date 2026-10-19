#pragma once

#include "cmd.h"
#include "errors.h"
#include "router_ids.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Remote commands: links, the dispatcher and the tree of module routers. Wire format, dispatcher and watch:
 * ROUTER.MD. The handler type, cmd_reply and ERR_CMD_* are cmd.h's; a command as an error names it (err_cmd_t) and
 * the error record are errors.h's. */

//#ref-defines router_flag @alias Request Flags @prefix ROUTER_FLAG_
#define ROUTER_FLAG_URGENT 0x01u  //@alias Urgent @description Run on the urgent task, next to (not behind) normal frames

//#ref-enum @alias Response Status
typedef enum {
  ROUTER_STATUS_OK = 0,  //@alias OK @description The command succeeded; the answer's data follows
  ROUTER_STATUS_ERROR = 1,  //@alias Error @description The command failed; the answer's data is the error record
} router_status_e;

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_ROUTER)
 * ------------------------------------------------------------------------- */

#define ERR_ROUTER_MAP(X)                                                                                    \
  X(ERR_ROUTER_KEY,         ERR_TAG(ERR_MOD_ROUTER, 1),  ERR_SEV_NONE,   { uint8_t key; }) /* context only: route step */ \
  X(ERR_ROUTER_NO_KEY,      ERR_TAG(ERR_MOD_ROUTER, 2),  ERR_SEV_HIGH,   {}) /* input ended where a key was expected */ \
  X(ERR_ROUTER_UNKNOWN_KEY, ERR_TAG(ERR_MOD_ROUTER, 3),  ERR_SEV_HIGH,   { uint8_t key; })                  \
  X(ERR_ROUTER_KEY_TAKEN,   ERR_TAG(ERR_MOD_ROUTER, 6),  ERR_SEV_HIGH,   { uint8_t key; })                  \
  X(ERR_ROUTER_TABLE_FULL,  ERR_TAG(ERR_MOD_ROUTER, 7),  ERR_SEV_HIGH,   { uint8_t key; })                  \
  X(ERR_ROUTER_BAD_LINK,    ERR_TAG(ERR_MOD_ROUTER, 8),  ERR_SEV_HIGH,   { uint8_t link; }) /* unknown, unregistered or taken */ \
  X(ERR_ROUTER_QUEUE_FULL,  ERR_TAG(ERR_MOD_ROUTER, 9),  ERR_SEV_MEDIUM, { uint8_t link; uint8_t urgent; }) /* frame dropped */ \
  X(ERR_ROUTER_FRAME_LEN,   ERR_TAG(ERR_MOD_ROUTER, 10), ERR_SEV_MEDIUM, { uint8_t link; uint16_t len; uint16_t max; }) /* frame too short or too long */

ERR_TAGS_DECLARE(ERR_ROUTER_MAP)

/* ----------------------------------------------------------------------------
 * Routing
 * ------------------------------------------------------------------------- */

typedef struct {
  uint8_t key;
  cmd_f fn;
} router_entry_t;

/**
 * @brief Calls the entry of `table` whose key is in[0], with the same `ctx`, and wraps a failure in ERR_ROUTER_KEY{key}.
 */
MUST_USE err_h router_call(const router_entry_t* table, uint32_t count, void* ctx, const uint8_t* in, uint32_t in_len,
                           uint8_t* out, uint32_t out_cap, uint32_t* out_len);

/**
 * @brief Copies the fixed-size payload after the key (in[1..]) into `args`; size 0 (args may be NULL) only checks
 * that nothing follows the key.
 * @return ERR_CMD_BAD_LEN unless exactly `size` bytes follow the key.
 */
MUST_USE err_h router_args(const uint8_t* in, uint32_t in_len, void* args, uint32_t size);

/**
 * @brief Like router_args for a fixed header and a variable tail: copies the header and points `tail` at the rest.
 * Size 0 (hdr may be NULL): the whole payload is the tail.
 * @return ERR_CMD_BAD_LEN unless the header and at least one tail byte follow the key.
 */
MUST_USE err_h router_args_tail(const uint8_t* in, uint32_t in_len, void* hdr, uint32_t size, const uint8_t** tail,
                                uint32_t* tail_len);

/* ----------------------------------------------------------------------------
 * Op macros (ROUTER.MD "Routing"). They write the cmd_f, check and parse the
 * payload; the block after them is the body, which sees `ctx`, `args`,
 * `tail` / `tail_len` (ROUTER_OP_TAIL) and `out`, `out_cap`, `out_len`.
 * ------------------------------------------------------------------------- */

// Op with a fixed-size payload `args_t` (packed struct).
#define ROUTER_OP(fn, args_t)                                           \
  static err_h fn##_body(void* ctx, const args_t* args, CMD_OUT_PARAMS); \
  static err_h fn(CMD_F_PARAMS) {                                   \
    args_t args;                                                           \
    ERR_TRY(router_args(in, in_len, &args, sizeof(args)));                    \
    return fn##_body(ctx, &args, out, out_cap, out_len);                   \
  }                                                                     \
  static err_h fn##_body(void* ctx, const args_t* args, CMD_OUT_PARAMS)

// Op without payload.
#define ROUTER_OP_NOARGS(fn)                               \
  static err_h fn##_body(void* ctx, CMD_OUT_PARAMS);   \
  static err_h fn(CMD_F_PARAMS) {                      \
    ERR_TRY(router_args(in, in_len, NULL, 0));             \
    return fn##_body(ctx, out, out_cap, out_len);          \
  }                                                        \
  static err_h fn##_body(void* ctx, CMD_OUT_PARAMS)

// Op with a fixed header `hdr_t` and a variable tail (name, data).
#define ROUTER_OP_TAIL(fn, hdr_t)                                                                        \
  static err_h fn##_body(void* ctx, const hdr_t* args, const uint8_t* tail, uint32_t tail_len,              \
                         CMD_OUT_PARAMS);                                                            \
  static err_h fn(CMD_F_PARAMS) {                                                                    \
    hdr_t args;                                                                                             \
    const uint8_t* tail;                                                                                 \
    uint32_t tail_len;                                                                                   \
    ERR_TRY(router_args_tail(in, in_len, &args, sizeof(args), &tail, &tail_len));                              \
    return fn##_body(ctx, &args, tail, tail_len, out, out_cap, out_len);                                    \
  }                                                                                                      \
  static err_h fn##_body(void* ctx, const hdr_t* args, const uint8_t* tail, uint32_t tail_len, CMD_OUT_PARAMS)

// Declares a module router in its header.
#define ROUTER_MODULE_DECLARE(fn) MUST_USE err_h fn(CMD_F_PARAMS)

// Defines the module router `fn`: consumes its own key and calls the op of `table`. Prefix `static` for a file-local one.
#define ROUTER_MODULE(fn, table)                                                             \
  err_h fn(CMD_F_PARAMS) {                                                               \
    return router_call(table, ARRAY_LEN(table), ctx, in + 1, in_len - 1, out, out_cap, out_len); \
  }

/** @brief Binds a root key (router_ids.h) to a module router (called with ctx NULL). Boot only, before router_init. */
MUST_USE err_h router_register(uint8_t key, cmd_f fn);

/**
 * @brief Routes one command (`in` starts at the root key): the dispatcher, and the replay of stored frames. Urgent and
 * normal frames run at the same time, so module routers must be thread-safe.
 */
MUST_USE err_h router_route(const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_cap, uint32_t* out_len);

/* ----------------------------------------------------------------------------
 * Links and dispatch. One client per link.
 * ------------------------------------------------------------------------- */

typedef struct {
  const char* name;
  /** Sends one response frame to the link's client. Both router tasks call it. NULL only for the internal link. */
  MUST_USE err_h (*send)(const uint8_t* frame, uint32_t len);
  /** Largest frame the link carries right now; NULL = CONFIG_ROUTER_FRAME_MAX. */
  uint32_t (*max_frame)(void);
} router_link_t;

/** @brief Registers a link under its ID (transport_e). Boot only; links are never removed. */
MUST_USE err_h router_link_register(uint8_t link_id, const router_link_t* link);

/**
 * @brief Queues one request frame from a link (copied). The link's task, not an ISR. A frame that can't be queued
 * (bad length, queue full) is answered with an error right away and reported; NULL is returned then.
 * @return Only misuse: ERR_ROUTER_BAD_LINK, ERR_CMD_BAD_ARG.
 */
MUST_USE err_h router_submit(uint8_t link_id, const uint8_t* frame, uint32_t len);

/* ----------------------------------------------------------------------------
 * Internal link, tap and origin
 * ------------------------------------------------------------------------- */

#define ROUTER_LINK_INTERNAL 0u  // the board itself: answers dropped, errors reported, not shown to the watch

/**
 * @brief Queues keys + payload (no seq / flags) from the board itself on the internal link. Never blocks. Task
 * context, not an ISR.
 * @return ERR_ROUTER_FRAME_LEN (too long for a frame), ERR_ROUTER_QUEUE_FULL.
 */
MUST_USE err_h router_inject(const uint8_t* req, uint32_t len, bool urgent);

/** Sees every frame the dispatcher routes (every link, also the internal one), on the routing task. */
typedef struct {
  /** Before routing. true = taken: answered OK with no data and not routed. */
  bool (*take)(const err_cmd_t* cmd);
  /** After routing a frame that wasn't taken. `result` is borrowed. */
  void (*seen)(const err_cmd_t* cmd, err_h result);
} router_tap_t;

/** @brief Sets the tap (one; NULL removes it). Boot only. */
void router_set_tap(const router_tap_t* tap);

/** @brief The command the calling task is routing; NULL outside the router tasks. Inside a replay: the outer one. */
const err_cmd_t* router_origin(void);

/**
 * @brief Sends the first `len` bytes of the calling op's `out` as an extra OK answer (same seq and link) before the
 * op's own answer: a command that answers in several packets. Inside a replay nothing is sent. Router tasks only.
 * @return ERR_CMD_BAD_ARG outside a router task. A failed send is reported, not returned.
 */
MUST_USE err_h router_answer_part(const uint8_t* out, uint32_t len);

/* ----------------------------------------------------------------------------
 * Watch: for the module that watches the clients (the dead-man, sys_system.h).
 * ------------------------------------------------------------------------- */

typedef struct {
  /** A frame arrived from a link (not the internal one), before it is queued. The link's task. */
  void (*frame)(void);
  /** After router_wake_urgent, on the urgent task (may block on I/O). */
  void (*urgent)(void);
} router_watch_t;

/** @brief Sets the watch (one; NULL removes it). Members may be NULL. Boot only, before router_init. */
void router_set_watch(const router_watch_t* watch);

/** @brief Runs the watch's `urgent` on the urgent task, soon. Any task or a timer callback, not an ISR. */
void router_wake_urgent(void);

/** @brief Starts the urgent and work tasks. Module routers are registered before; links before or after. */
MUST_USE err_h router_init(void);

#ifdef __cplusplus
}
#endif
