#pragma once

#include "errors.h"
#include "router.h"
#include "router_queue_errors.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The commands of this board with their origin and their answers: the queue and the two tasks that run them (the work
 * task and the urgent one). Whoever hands the router a command (the session, the board itself) gives the callback its
 * answer goes to (router_done_f). The routing itself, the handler type and the op tables are router.h's. ROUTER_QUEUE.MD. */

/* ----------------------------------------------------------------------------
 * A command's flags and an answer's status
 * ------------------------------------------------------------------------- */

/* A command's flags (cmd_req_t.flags): the one definition of them. The session's frame byte carries them at these
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

#define ROUTER_SRC_SELF 0xFEu  // cmd_req_t.src of the board's own commands (router_inject): SYS_SESSION_ID_SELF
#define ROUTER_SRC_NONE 0xFDu  // cmd_req_t.src of a command with no peer (a provider's command binding): SYS_SESSION_ID_NONE

/**
 * @brief A command as it came in: its request [key][dir][code][args] and its origin. Whoever submits it to the router
 * fills it (the session from a frame's header, a watch, the board's own code); an error the command raises names it.
 * errors.h knows it only as `struct cmd_req` and passes it on to its sink without reading it.
 */
typedef struct cmd_req {
  const uint8_t* req;  // may be NULL when req_len is 0
  uint32_t req_len;
  uint16_t seq;        // the sender's counter, in every answer
  uint8_t src;         // who sent it: a board or client id; ROUTER_SRC_SELF (0xFE) for the board's own commands
  uint8_t flags;       // ROUTER_FLAG_*
} cmd_req_t;

/**
 * @brief The error record of a chain (ERRORS.MD "Error record"): of the command `cmd` (err_record_cmd), or an
 * internal one when `cmd` is NULL (err_record). The bytes of an ERROR answer and of the errors stream.
 * @param[in]  cmd   The command, or NULL for an internal error
 * @param[in]  chain The error chain
 * @param[out] buf   Buffer for the record
 * @param[in]  cap   Size of buf
 * @return Bytes written (0 only when cap is too small for the header).
 */
uint32_t cmd_err_record(const cmd_req_t* cmd, err_h chain, uint8_t* buf, uint32_t cap);

/**
 * @brief Binds every module router queued via ROUTER_MODULE_REGISTER (router_root_init), then starts the urgent
 * and work tasks.
 * @return ERR_CMD_BAD_ARG / ERR_ROUTER_KEY_TAKEN / ERR_ROUTER_TABLE_FULL of a registered router, or the error of the
 *         task start
 */
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
 * @param data    The answer (OK) or the error record (ERROR), in the task's own buffer, valid for the call only: a
 *                callback that frames it sends its header and `data` as the two parts of one send (sys_data_provider).
 * @param len     Bytes in `data`, at most CMD_DATA_MAX.
 * @return NULL; else an error the router reports with the command (the answer is lost: a reader retries).
 */
typedef MUST_USE err_h (*router_done_f)(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data,
                                        uint32_t len);

/**
 * @brief Queues one command: `cmd` and its request are copied, so both may go after the call. Never blocks; any task
 * but an ISR, a provider driver's RX task too.
 * @param[in] cmd  The origin (src, seq, flags) and the request ([key][dir][...], 2..CMD_DATA_MAX bytes).
 * @param[in] done Where its answer goes; NULL: nowhere (a failure is reported only).
 * @param[in] ctx  Given to `done`.
 * @return NULL when queued; ERR_ROUTER_FRAME_LEN, ERR_ROUTER_QUEUE_FULL: not queued, the caller answers it.
 */
MUST_USE err_h router_submit(const cmd_req_t* cmd, router_done_f done, void* ctx);

/**
 * @brief Queues a command of the board itself: router_submit with src ROUTER_SRC_SELF and seq from the router's own
 * counter (the actions, the project's start, a call of the board to itself).
 * @param[in] req   [key][dir][...].
 * @param[in] flags ROUTER_FLAG_URGENT, ROUTER_FLAG_NOREPLY.
 * @param[in] done  Where its answer goes; NULL: nowhere.
 * @param[in] ctx   Given to `done`.
 * @return NULL when queued; ERR_ROUTER_FRAME_LEN, ERR_ROUTER_QUEUE_FULL.
 */
MUST_USE err_h router_inject(cmd_in_t req, uint8_t flags, router_done_f done, void* ctx);

/**
 * @brief Ends a command's chain: logged with the command ("cmd src f0 seq 7 flags 00 req 02 02 00") and handed to the
 * error sink with it (its record on the errors stream). For a command the router refused, by whoever answers it.
 * @param[in] cmd The command
 * @param[in] err The chain that ended it
 */
void router_report_cmd(const cmd_req_t* cmd, err_h err);

/* ----------------------------------------------------------------------------
 * Origin
 * ------------------------------------------------------------------------- */

/**
 * @brief The command the calling task is routing, valid until its handler returns. Inside a replay: the outer one.
 * @return The origin; NULL outside the router tasks.
 */
const cmd_req_t* router_origin(void);

/**
 * @brief Whether the caller runs on a router task (urgent or work): what waits for an answer must not run there.
 * @return true on a router task
 */
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
