#pragma once

#include "cmd.h"
#include "errors.h"
#include "router_ids.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A module router is a cmd_f (cmd.h) that routes by keys. Every remote
 * command goes through routers: each one consumes its key from `in`, passes
 * the rest to the next router, and on failure wraps the returned error with
 * its key, so the error chain records the route. `in` starts at the router's
 * own key; `ctx` is NULL from the root (router_route) and what the caller
 * hands down below it (router_call).
 *
 * The handler type, the answer helper (cmd_reply) and the payload errors
 * (ERR_CMD_*) live in cmd.h: the router doesn't own them, so code that only
 * handles commands (the device registry, the contracts, the devices) doesn't
 * depend on it.
 */

/* ----------------------------------------------------------------------------
 * Wire format
 *
 * Request (one frame from a link):
 *   [seq][flags][key][key]...[payload]
 *   seq   - client's counter, echoed in the response
 *   flags - ROUTER_FLAG_*; the app decides what is urgent
 *   keys  - root key (router_ids.h), then each router's own keys
 *
 * Response (sent to the request's origin only):
 *   [CONFIG_ROUTER_RESPONSE_STREAM][seq][status][data]
 *   status 0 - data is the router's output (may be empty)
 *   status 1 - data is the error record (router_err_record): the request and
 *              its error chain together, so the error can be tracked alone
 * ------------------------------------------------------------------------- */

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

// clang-format off
/* Tags 4, 5 and 11 (BAD_LEN, OUT_FULL, BAD_ARG) moved to ERR_MOD_CMD (cmd.h); their numbers stay retired. */
#define ERR_ROUTER_MAP(X)                                                                                    \
  X(ERR_ROUTER_KEY,         ERR_TAG(ERR_MOD_ROUTER, 1),  ERR_SEV_NONE,   { uint8_t key; }) /* context only: route step */ \
  X(ERR_ROUTER_NO_KEY,      ERR_TAG(ERR_MOD_ROUTER, 2),  ERR_SEV_HIGH,   {}) /* input ended where a key was expected */ \
  X(ERR_ROUTER_UNKNOWN_KEY, ERR_TAG(ERR_MOD_ROUTER, 3),  ERR_SEV_HIGH,   { uint8_t key; })                  \
  X(ERR_ROUTER_KEY_TAKEN,   ERR_TAG(ERR_MOD_ROUTER, 6),  ERR_SEV_HIGH,   { uint8_t key; })                  \
  X(ERR_ROUTER_TABLE_FULL,  ERR_TAG(ERR_MOD_ROUTER, 7),  ERR_SEV_HIGH,   { uint8_t key; })                  \
  X(ERR_ROUTER_BAD_LINK,    ERR_TAG(ERR_MOD_ROUTER, 8),  ERR_SEV_HIGH,   { uint8_t link; }) /* unknown, unregistered or taken */ \
  X(ERR_ROUTER_QUEUE_FULL,  ERR_TAG(ERR_MOD_ROUTER, 9),  ERR_SEV_MEDIUM, { uint8_t link; uint8_t urgent; }) /* frame dropped */ \
  X(ERR_ROUTER_FRAME_LEN,   ERR_TAG(ERR_MOD_ROUTER, 10), ERR_SEV_MEDIUM, { uint8_t link; uint16_t len; uint16_t max; }) /* frame too short or too long */
// clang-format on

ERR_TAGS_DECLARE(ERR_ROUTER_MAP)

/* ----------------------------------------------------------------------------
 * Error record
 *
 * An error with what caused it, self-contained, for responses now and for the
 * errors stream / stored history later:
 *
 *   [src u8]                                   router_err_src_e
 *   src COMMAND:
 *     [link u8][peer u32 LE][seq u8][flags u8]
 *     [req_len u8]  bits 0-6 = request bytes that follow, bit 7 = request cut
 *     [request]     keys + payload as received (no seq / flags)
 *   [chain]                                    err_encode() format
 *
 * The request is cut to CONFIG_ROUTER_ERR_REQ_MAX bytes, and further when the
 * buffer is small; the chain gets the rest (its own truncated bit).
 * ------------------------------------------------------------------------- */

//#ref-enum @alias Error Record Source
typedef enum {
  ROUTER_ERR_SRC_INTERNAL = 0,  //@alias Internal @description no request (task, timer, boot); later a device ID / task may follow
  ROUTER_ERR_SRC_COMMAND = 1,  //@alias Command @description raised while running a command
} router_err_src_e;

//#ref-defines router_err_flag @alias Error Record Flags @prefix ROUTER_ERR_
#define ROUTER_ERR_REQ_CUT 0x80u  //@alias Request cut @description Bit 7 of the request length: the request was cut to fit, more of it was sent

/**
 * @brief The command an error belongs to.
 */
typedef struct {
  uint32_t peer;
  const uint8_t* req;  // keys + payload (frame without seq / flags), may be NULL when req_len is 0
  uint32_t req_len;
  uint8_t link;
  uint8_t seq;
  uint8_t flags;
} router_cmd_t;

/**
 * @brief Encodes an error record (layout above) into `buf`.
 * @param cmd The command, or NULL for an internal error (src INTERNAL).
 * @return Bytes written (0 only when cap is too small for the header).
 */
uint32_t router_err_record(const router_cmd_t* cmd, err_h err, uint8_t* buf, uint32_t cap);

/* ----------------------------------------------------------------------------
 * Routing
 * ------------------------------------------------------------------------- */

/**
 * @brief One entry of a module's key table.
 */
typedef struct {
  uint8_t key;
  cmd_f fn;
} router_entry_t;

/**
 * @brief Calls the entry of `table` whose key is in[0], with the same `ctx`,
 * and wraps a failure in ERR_ROUTER_KEY{key}. A module router consumes its
 * own key and hands the rest to this (ROUTER_MODULE writes that router).
 */
MUST_USE err_h router_call(const router_entry_t* table, uint32_t count, void* ctx, const uint8_t* in, uint32_t in_len,
                           uint8_t* out, uint32_t out_cap, uint32_t* out_len);

/**
 * @brief Copies the fixed-size payload after the key (in[1..]) into `args`.
 * With size 0 (args may be NULL) it only checks that nothing follows the key.
 * @return ERR_CMD_BAD_LEN unless exactly `size` bytes follow the key.
 * @usage op_args_t a; ERR_TRY(router_args(in, in_len, &a, sizeof(a)));
 */
MUST_USE err_h router_args(const uint8_t* in, uint32_t in_len, void* args, uint32_t size);

/**
 * @brief Like router_args, for a fixed header followed by a variable tail (a
 * name, data bytes): copies the header and points `tail` at the rest. With
 * size 0 (hdr may be NULL) the whole payload is the tail.
 * @return ERR_CMD_BAD_LEN unless the header and at least one tail byte follow the key.
 */
MUST_USE err_h router_args_tail(const uint8_t* in, uint32_t in_len, void* hdr, uint32_t size, const uint8_t** tail,
                                uint32_t* tail_len);

/* ----------------------------------------------------------------------------
 * Op macros
 *
 * A module router is a key table of ops; each op only adapts the native API
 * (which stays the source: typed, callable without packing). The macros write
 * the cmd_f, check the payload length and parse it; the block after them is
 * the op body, which sees:
 *   ctx                     what router_call was given (a device's struct), else NULL
 *   a                       const args_t* - the parsed payload (ROUTER_OP, ROUTER_OP_TAIL)
 *   tail, tail_len          bytes after the header, at least 1 (ROUTER_OP_TAIL)
 *   out, out_cap, out_len   the answer (cmd_reply); untouched = empty answer
 *
 *   ROUTER_OP(op_char_remove, char_remove_t) {
 *     return sys_ble_char_remove(a->svc_uuid, a->uuid);
 *   }
 *   ROUTER_OP_NOARGS(op_apply) {
 *     return sys_ble_apply();
 *   }
 *   static const router_entry_t k_ops[] = {{OP_CHAR_REMOVE, op_char_remove}, {OP_APPLY, op_apply}};
 *   ROUTER_MODULE(sys_ble_router, k_ops)
 * ------------------------------------------------------------------------- */

// Op with a fixed-size payload `args_t` (packed struct).
#define ROUTER_OP(fn, args_t)                                           \
  static err_h fn##_body(void* ctx, const args_t* a, CMD_OUT_PARAMS); \
  static err_h fn(CMD_F_PARAMS) {                                   \
    args_t a;                                                           \
    ERR_TRY(router_args(in, in_len, &a, sizeof(a)));                    \
    return fn##_body(ctx, &a, out, out_cap, out_len);                   \
  }                                                                     \
  static err_h fn##_body(void* ctx, const args_t* a, CMD_OUT_PARAMS)

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
  static err_h fn##_body(void* ctx, const hdr_t* a, const uint8_t* tail, uint32_t tail_len,              \
                         CMD_OUT_PARAMS);                                                            \
  static err_h fn(CMD_F_PARAMS) {                                                                    \
    hdr_t a;                                                                                             \
    const uint8_t* tail;                                                                                 \
    uint32_t tail_len;                                                                                   \
    ERR_TRY(router_args_tail(in, in_len, &a, sizeof(a), &tail, &tail_len));                              \
    return fn##_body(ctx, &a, tail, tail_len, out, out_cap, out_len);                                    \
  }                                                                                                      \
  static err_h fn##_body(void* ctx, const hdr_t* a, const uint8_t* tail, uint32_t tail_len, CMD_OUT_PARAMS)

// Declares a module router (in its header): MUST_USE err_h fn(cmd_f parameters);
#define ROUTER_MODULE_DECLARE(fn) MUST_USE err_h fn(CMD_F_PARAMS)

/* Defines the module router `fn` (a cmd_f): consumes its own key and calls
 * the op of `table` (router_call). Prefix `static` for a file-local one. */
#define ROUTER_MODULE(fn, table)                                                             \
  err_h fn(CMD_F_PARAMS) {                                                               \
    return router_call(table, ARRAY_LEN(table), ctx, in + 1, in_len - 1, out, out_cap, out_len); \
  }

/**
 * @brief Binds a root key (router_ids.h) to a module router (called with ctx
 * NULL). Boot only, before router_init().
 */
MUST_USE err_h router_register(uint8_t key, cmd_f fn);

/**
 * @brief Routes one command (`in` starts at the root key). Used by the
 * dispatcher and for replay of stored frames. Thread-safe as far as the
 * module routers are: urgent and normal frames run at the same time.
 */
MUST_USE err_h router_route(const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_cap, uint32_t* out_len);

/* ----------------------------------------------------------------------------
 * Links and dispatch
 * ------------------------------------------------------------------------- */

/**
 * @brief A transport that delivers command frames (router_submit) and carries
 * the answers. `peer` tells clients on one link apart (0 when there is one).
 */
typedef struct {
  const char* name;
  /** Sends one response frame to `peer`. */
  MUST_USE err_h (*send)(uint32_t peer, const uint8_t* frame, uint32_t len);
  /** Largest frame the link carries right now; NULL = CONFIG_ROUTER_FRAME_MAX. */
  uint32_t (*max_frame)(uint32_t peer);
} router_link_t;

/**
 * @brief Registers a link under its ID (router_ids.h). Any time; links are
 * never removed.
 */
MUST_USE err_h router_link_register(uint8_t link_id, const router_link_t* link);

/**
 * @brief Queues one whole request frame from a link (copied). Called from the
 * link's own task, not from an ISR. A frame that can't be queued (bad length,
 * queue full) is answered with an error response right away (when it has a
 * seq) and reported with its command (err_report_ex); NULL is returned then.
 * @return Only misuse: ERR_ROUTER_BAD_LINK, ERR_CMD_BAD_ARG.
 */
MUST_USE err_h router_submit(uint8_t link_id, uint32_t peer, const uint8_t* frame, uint32_t len);

/**
 * @brief The link lost its client (e.g. BLE disconnect). Triggers the safe
 * state when the dead-man is enabled with `on_link_lost`.
 */
void router_link_lost(void);

/* ----------------------------------------------------------------------------
 * Internal link, tap and origin
 *
 * Link 0 is the board itself: frames queued by router_inject (an action run
 * by an event, the VM, boot). They are routed like link frames, on the same
 * tasks, and their answers are dropped; their errors are still reported
 * (link "internal"). They don't feed the dead-man: they don't prove a client
 * is alive.
 * ------------------------------------------------------------------------- */

#define ROUTER_LINK_INTERNAL 0u

/**
 * @brief Queues a command from the board itself on the internal link. Never
 * blocks. Task context, not an ISR.
 * @param req    Keys + payload (no seq / flags), as router_route takes them.
 * @param urgent Run on the urgent task.
 * @return ERR_ROUTER_FRAME_LEN (too long for a frame), ERR_ROUTER_QUEUE_FULL.
 * @usage ERR_TRY(router_inject((const uint8_t[]){ROUTER_KEY_SYS, 0x02}, 2, true));
 */
MUST_USE err_h router_inject(const uint8_t* req, uint32_t len, bool urgent);

/**
 * @brief Sees every frame the dispatcher routes, from every link (also the
 * internal one), on the task that routes it. Both members may be NULL.
 */
typedef struct {
  /** Before routing. true = the frame is taken: answered OK with no data and not routed. */
  bool (*take)(const router_cmd_t* cmd);
  /** After routing a frame that wasn't taken. `result` is borrowed (don't free it). */
  void (*seen)(const router_cmd_t* cmd, err_h result);
} router_tap_t;

/**
 * @brief Sets the tap (one; NULL removes it). Boot only, by the app: the
 * router doesn't know who records.
 */
void router_set_tap(const router_tap_t* tap);

/**
 * @brief The command the calling task is routing (link, peer, request), so an
 * op can tell who sent it. NULL outside the router tasks. Inside a replay
 * (router_route called by an op) it is still the outer command.
 */
const router_cmd_t* router_origin(void);

/**
 * @brief Sends the first `len` bytes of `out` — the buffer the calling op got — as an extra OK answer to the command
 * being routed (same seq, same link and peer), before the op's own answer. For a command that answers with several
 * packets (an action read back frame by frame): the op sends all but the last this way and returns the last as its
 * answer. The client matches them by seq and knows the count itself. Inside a replay (an action running the op) `out`
 * is not the dispatcher's buffer and nothing is sent, like any answer there. Router tasks only.
 * @return ERR_CMD_BAD_ARG outside a router task. A link that fails to send is reported, not returned.
 */
MUST_USE err_h router_answer_part(const uint8_t* out, uint32_t len);

/* ----------------------------------------------------------------------------
 * Safe state and dead-man
 *
 * The safe-state handler is registered by the app (it knows what "safe" is:
 * VM stop, devices suspended). It runs on the urgent task.
 * ------------------------------------------------------------------------- */

//#ref-enum @alias Safe State Reason
typedef enum {
  ROUTER_SAFE_STOP = 1,  //@alias Stop @description sys STOP command
  ROUTER_SAFE_DEADMAN = 2,  //@alias Deadman @description no frame within the dead-man timeout
  ROUTER_SAFE_LINK_LOST = 3,  //@alias Link lost @description router_link_lost with on_link_lost set
} router_safe_reason_e;

typedef void (*router_safe_state_f)(router_safe_reason_e reason);

/**
 * @brief Sets the safe-state handler. Boot only. NULL = only logged.
 */
void router_set_safe_state(router_safe_state_f fn);

/**
 * @brief Runtime dead-man. `timeout_ms` 0 = off. While on, the safe state is
 * entered when no frame arrives from any link for `timeout_ms` (once; the
 * next frame re-arms it), and on router_link_lost when `on_link_lost`.
 * Not stored - off again after a restart (default CONFIG_ROUTER_DEADMAN_DEFAULT_MS).
 */
void router_deadman_set(uint32_t timeout_ms, bool on_link_lost);
void router_deadman_get(uint32_t* timeout_ms, bool* on_link_lost);

/**
 * @brief Registers the sys router and starts the urgent and work tasks.
 * Register module routers before; links may come before or after.
 */
MUST_USE err_h router_init(void);

#ifdef __cplusplus
}
#endif
