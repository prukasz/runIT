#pragma once

#include "cmd.h"
#include "cmd_types.h"
#include "data_matrix.h"
#include "errors.h"
#include "router.h"
#include "router_id.h"
#include "router_queue_errors.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The commands of this board with their origin and their answers: the queue and the two tasks that run them (the work
 * task and the urgent one), the router's port in the data matrix, and this board's calls. A command comes from the
 * port (a frame, answered to the port it came from) or from C (router_submit, router_inject: answered to a callback,
 * router_done_f). The routing itself, the handler type and the op tables are router.h's. ROUTER_QUEUE.MD. */

/* ----------------------------------------------------------------------------
 * A command's flags and an answer's status
 * ------------------------------------------------------------------------- */

/* A command's flags (cmd_req_t.flags): the one definition of them. The header's type byte carries them at these bits
   (cmd.h, CMD_TYPE_FLAGS). */
//#ref-defines router_flag @alias Request Flags @prefix ROUTER_FLAG_
#define ROUTER_FLAG_URGENT  (1u << 0)  //@alias Urgent @description Runs on the urgent task, next to normal commands
#define ROUTER_FLAG_NOREPLY (1u << 1)  //@alias No reply @description No answer; a failure goes to the errors stream

/** @brief What an answer is: the header's type byte carries it (cmd.h, CMD_TYPE_STATUS). */
//#ref-enum @alias Response Status
typedef enum {
  ROUTER_STATUS_OK = 0,     //@alias OK @description The command succeeded; the answer's data follows
  ROUTER_STATUS_ERROR = 1,  //@alias Error @description The command failed; the answer's data is the error record
} router_status_e;

_Static_assert((ROUTER_FLAG_URGENT | ROUTER_FLAG_NOREPLY) == CMD_TYPE_FLAGS, "the type byte's flags");
_Static_assert(ROUTER_FLAG_URGENT == CMD_SEND_URGENT, "ROUTER_FLAG_URGENT must match CMD_SEND_URGENT");
_Static_assert(ROUTER_STATUS_ERROR << 2 == CMD_TYPE_STATUS, "the type byte's status bit");

/* A command: cmd_req_t (cmd_types.h, in utils: the errors component names it too). */

/* The router's port (DATA_MATRIX.MD): frames in, [cmd_hdr_t][body]. A REQUEST is run and answered to the port it
   came from (data_matrix_send_to); an ANSWER ends this board's call it answers (router_request). Linked with the
   sessions' command sub-ports and the providers that carry commands with no session. */
#define ROUTER_PORT DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, MODULE_ID_ROUTER)

/**
 * @brief Binds every module router queued via ROUTER_MODULE_REGISTER (router_root_init), registers the router's port
 * (ROUTER_PORT), starts the calls' timer and the urgent and work tasks. After data_matrix's start.
 * @return ERR_CMD_BAD_ARG / ERR_ROUTER_KEY_TAKEN / ERR_ROUTER_TABLE_FULL of a registered router, the port's
 *         registration error, or the error of the task start
 */
MUST_USE err_h router_init(void);

/* ----------------------------------------------------------------------------
 * Commands in, answers out: every command comes with the callback its answer goes to, so the router keeps nothing of
 * who sent it.
 * ------------------------------------------------------------------------- */

/**
 * @brief Where a command's answer goes when it came from C (a module that wants the answer, a call). On the router
 * task that ran the command; never for a NOREPLY one. It may wait for room on the way back, at most its own bound.
 * @param ctx     The submitter's.
 * @param cmd     The command answered (its src, seq, flags, request).
 * @param status  router_status_e.
 * @param data    The answer (OK) or the error record (ERROR), in the task's own buffer, valid for the call only: a
 *                callback that frames it copies it with its header into the flat frame it sends out of its stream port.
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
 * @brief Queues a command of the board itself: router_submit with src CMD_SRC_SELF and seq from the router's own
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
 * This board's calls: a command run on this board or out of the router's port, its answer to a callback
 * ------------------------------------------------------------------------- */

/**
 * @brief Runs a command and calls `cb` with its answer, as for a command the router ran: one way to run a command with
 * its answer, here or on the other end of a link. Never blocks, any task (a device op too).
 *  - `to` ROUTER_PORT, this board: router_inject(); the answer on the router task, no timeout.
 *  - Else a port linked from the router's (a session's command sub-port): a call slot keeps it under a new seq and the
 *    request goes out (NOWAIT) until its answer, on the task that brings it, ends the call. An ERROR's record has
 *    ERR_REMOTE{board, seq} before that board's chain. No answer in the timeout: ERROR with this board's record of
 *    ERR_ROUTER_CALL_TIMEOUT; router_request_fail_to(to): of ERR_ROUTER_CALL_DOWN (on the board timers' task, or the
 *    caller's of router_request_fail_to).
 * Every call out is answered. `cb`'s cmd is the call: its request (the first CONFIG_ERRORS_RECORD_REQ_MAX bytes), its
 * seq, src CMD_SRC_SELF.
 * @param[in] to         ROUTER_PORT, or the port the request goes to.
 * @param[in] req        The command: [key][dir][code | id + op][args], 2..CMD_DATA_MAX + 1 bytes (the key goes into
 *                       the header).
 * @param[in] flags      ROUTER_FLAG_URGENT, or 0.
 * @param[in] timeout_ms Without an answer this long: ERR_ROUTER_CALL_TIMEOUT; 0: CONFIG_ROUTER_CALL_TIMEOUT_MS.
 * @param[in] cb         The answer; NULL: nobody wants it.
 * @param[in] ctx        Given to `cb`.
 * @return NULL when sent; ERR_CMD_BAD_ARG, ERR_ROUTER_CALLS_FULL, ERR_ROUTER_NO_LINK, or (to this board) what
 *         router_inject returns.
 */
MUST_USE err_h router_request(data_matrix_port_id_t to, cmd_in_t req, uint8_t flags, uint32_t timeout_ms,
                              router_done_f cb, void* ctx);

/**
 * @brief router_request, waited for: the caller's task sleeps until the call ends, at most its timeout and a margin of
 * its own, so a wait never hangs, also on a task the answer or the timer would need. Not to this board on a router
 * task.
 * @param[in]  to         As router_request.
 * @param[in]  req        As router_request.
 * @param[in]  flags      As router_request.
 * @param[in]  timeout_ms As router_request.
 * @param[out] buf        The answer.
 * @param[in]  cap        Bytes `buf` holds.
 * @param[out] len        The answer's bytes.
 * @return NULL with the answer; its error: ERR_REMOTE{board, seq} over that board's chain, ERR_ROUTER_CALL_TIMEOUT /
 *         _CALL_DOWN, ERR_CMD_BAD_LEN (longer than `cap`), ERR_ROUTER_WOULD_BLOCK (to this board on a router task), or
 *         what router_request returns.
 */
MUST_USE err_h router_request_wait(data_matrix_port_id_t to, cmd_in_t req, uint8_t flags, uint32_t timeout_ms,
                                   void* buf, uint32_t cap, uint32_t* len);

/**
 * @brief Ends every call that went out to `to` with ERR_ROUTER_CALL_DOWN: the link behind it ended (a session's end).
 * @param[in] to The port.
 */
void router_request_fail_to(data_matrix_port_id_t to);

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
