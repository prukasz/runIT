#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cmd.h"
#include "errors.h"
#include "router_queue.h"
#include "sys_data_provider.h"
#include "sys_session_config.h"
#include "sys_session_errors.h"

/**
 * @file sys_session.h
 * @brief Sessions (SYS_SESSION.MD): a board's way to its neighbours and the clients. A session is a provider attached
 * as a way, UP (to a client or this board's master) or DOWN (to a slave); the session layer hands the commands for
 * this board to the router, forwards the frames for other boards on the driver's RX task, carries the PUSH frames,
 * makes the board's calls (to another board or to itself) and tells who is reached and who is gone. It has no task and
 * no queue of its own: the driver's RX task and the board timers' task. Code knows a peer by its id: a board id or a
 * client id.
 * @usage ERR_TRY(sys_session_attach(link, SYS_SESSION_DOWN));
 *        ERR_TRY(sys_session_call(1, req, 0, 0, on_answer, ctx));
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * Constants and listener types
 * ------------------------------------------------------------------------- */

#define SYS_SESSION_TIMEOUT_MIN_MS 150u
#define SYS_SESSION_TIMEOUT_MAX_MS 60000u

/** A gone listener's `role` for a peer the master's PEER names gone: no session of this board ended. */
#define SYS_SESSION_VIA_NOTICE 0xFFu

/**
 * @brief A peer can no longer be reached. On the context that noticed it (a driver's RX task, the board timers' task,
 * the caller of sys_session_detach): drop what you keep for it and return; never block.
 * @param peer  Its board or client id; SYS_SESSION_ID_BROADCAST: the master's session ended, so every peer (a slave
 *              reaches all of them through its master).
 * @param role  sys_session_role_e of this board's session that ended (MASTER or CLIENT is what the dead-man answers),
 *              or SYS_SESSION_VIA_NOTICE (the master's PEER: a peer of it).
 */
typedef void (*sys_session_gone_f)(uint8_t peer, uint8_t role);

/**
 * @brief A peer can be reached: a session of this board opened with it. On the context that noticed it; never block.
 * @param peer  Its board or client id.
 */
typedef void (*sys_session_reached_f)(uint8_t peer);

/**
 * @brief A PUSH frame for this board, on the driver's RX task. Copy what you keep and return; never block.
 * @param src   The board or client that sent it.
 * @param lost  PUSH frames of its class (urgent or not) from `src` to this board lost just before it, of any stream or
 *              channel (SYS_SESSION.MD "Counting push frames"); 0 also when it is not known.
 * @param data  [stream id or channel][payload], valid for the call only.
 * @param len   Bytes in `data`.
 */
typedef void (*sys_session_push_f)(uint8_t src, uint16_t lost, const uint8_t* data, uint32_t len);

/* ----------------------------------------------------------------------------
 * Start and this board
 * ------------------------------------------------------------------------- */

/**
 * @brief Starts the session layer: this board's id (NVS, set by the app with ID; SYS_SESSION_ID_DEFAULT on a new
 * board), its boot id and its timer (pings, session and call timeouts, on the board timers' task). Boot, after the NVS
 * partitions and router_init(), before any attach.
 * @return NULL when started.
 */
MUST_USE err_h sys_session_init(void);

/** @brief This board's id. */
uint8_t sys_session_get_board_id(void);

/**
 * @brief Whether this board has no master: it is the root, the board the app talks to. Any task.
 * @return true for the root
 */
bool sys_session_is_root(void);

/**
 * @brief Keeps a new board id in NVS: it is this board's id from the next start (ID).
 * @param[in] board_id 0..SYS_SESSION_ID_LAST.
 * @return NULL when kept; ERR_CMD_BAD_ARG, the NVS error.
 */
MUST_USE err_h sys_session_set_board_id(uint8_t board_id);

/**
 * @brief Sets how long this board waits in silence on its master's and its clients' sessions before it ends them: the
 * dead-man's time (sys_system's DEADMAN). Sends a hello on each, so the peer sends at least every third of it. Slaves'
 * sessions keep CONFIG_SYS_SESSION_TIMEOUT_MS. Any task.
 * @param[in] ms SYS_SESSION_TIMEOUT_MIN_MS..SYS_SESSION_TIMEOUT_MAX_MS; CONFIG_SYS_SESSION_TIMEOUT_MS at boot.
 * @return NULL when set; ERR_CMD_BAD_ARG.
 */
MUST_USE err_h sys_session_set_timeout(uint32_t ms);

/* ----------------------------------------------------------------------------
 * Sessions
 * ------------------------------------------------------------------------- */

/**
 * @brief Makes a provider a way to a peer: opens it and, when it is up, sends the hello. Until the peer's hello comes,
 * only control frames pass. The same way again on an attached provider is done (a stored record run at every start).
 * @param[in] provider A framed provider (SYS_DATA_PROVIDER_STATE_FRAMED).
 * @param[in] way      sys_session_way_e: UP (a client or this board's master: the hello says which) or DOWN (a slave).
 * @return NULL when attached; ERR_SESSION_FULL, ERR_SESSION_NOT_FRAMED (a raw provider), ERR_CMD_BAD_ARG (attached as
 *         the other way, a bad way), or the provider's open error (ERR_PROV_NOT_FOUND, _IN_USE).
 */
MUST_USE err_h sys_session_attach(uint8_t provider, uint8_t way);

/**
 * @brief Ends the session on a provider and closes it: its peer is gone.
 * @param[in] provider The provider.
 * @return NULL when detached or not attached; the provider's close error.
 */
MUST_USE err_h sys_session_detach(uint8_t provider);

/**
 * @brief Whether a peer is the peer of an open session on a WIRED provider: a neighbour on a cable, where a caller may
 * wait for an answer. Any task.
 * @param[in] peer A board id.
 * @return true when wired and open.
 */
bool sys_session_wired(uint8_t peer);

/* ----------------------------------------------------------------------------
 * Calls
 * ------------------------------------------------------------------------- */

/**
 * @brief Runs a command on a board, this one too, and calls `cb` with its answer as the router does for a command it
 * ran: one way to run a command with its answer, here or there. Never blocks, any task (a device op too).
 *  - To this board (its id or SYS_SESSION_ID_SELF): router_inject(); the answer on the router task, no timeout.
 *  - To another: the request goes out (src this board, NOWAIT on the session's provider) and a call slot keeps it
 *    until the answer, on the RX task it came on, which ends the call. An ERROR's record has ERR_REMOTE{board, seq}
 *    before that board's chain. No answer in the timeout: ERROR with this board's record of ERR_SESSION_TIMEOUT; the
 *    session it went through ended: of ERR_SESSION_PEER_DOWN (both on the board timers' task or the RX task).
 * Every call to another board is answered: NOREPLY goes to a broadcast only. A slave's broadcast goes to its master,
 * which runs it and sends it to every slave; the root's goes to every slave only: the root has done what it broadcasts.
 * `cb`'s cmd is the call: its request (the first CONFIG_ERRORS_RECORD_REQ_MAX bytes), its seq, src this board.
 * @param[in] dst        A board (0..SYS_SESSION_ID_LAST), this one included, or SYS_SESSION_ID_BROADCAST (NOREPLY only).
 * @param[in] req        The command: [key][dir][code | id + op][args], at most CMD_DATA_MAX.
 * @param[in] flags      ROUTER_FLAG_URGENT; ROUTER_FLAG_NOREPLY with SYS_SESSION_ID_BROADCAST only (`cb` NULL).
 * @param[in] timeout_ms Without an answer this long: ERR_SESSION_TIMEOUT; 0: the way's own (the wired one to the peer
 *                       of a WIRED session, else CONFIG_SYS_SESSION_CALL_TIMEOUT_MS).
 * @param[in] cb         The answer (router_done_f; its return is reported); NULL with NOREPLY.
 * @param[in] ctx        Given to `cb`.
 * @return NULL when sent; ERR_CMD_BAD_ARG (NOREPLY to one board), ERR_SESSION_NO_ROUTE, ERR_SESSION_CALLS_FULL,
 *         ERR_SESSION_BUSY (CONFIG_SYS_SESSION_PEER_IN_FLIGHT to that board: later), the provider's ERR_PROV_BUSY / _DOWN
 *         / _TOO_LONG, or (to this board) the router's ERR_ROUTER_QUEUE_FULL / _FRAME_LEN.
 */
MUST_USE err_h sys_session_call(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, router_done_f cb,
                                void* ctx);

/**
 * @brief sys_session_call, waited for: the caller's task sleeps until the call ends, at most its timeout and a margin
 * of its own (SYS_SESSION.MD "Waiting for the answer"), so a wait never hangs, also on a task an answer or the timer
 * would need. For configuration (the flasher, a board's setup) over any way, and for a remote device to a WIRED
 * neighbour. Not to this board on a router task.
 * @param[in]  dst        A board, this one included (not SYS_SESSION_ID_BROADCAST).
 * @param[in]  req        The command: [key][dir][code | id + op][args], at most CMD_DATA_MAX.
 * @param[in]  flags      ROUTER_FLAG_URGENT, or 0.
 * @param[in]  timeout_ms As sys_session_call; 0: the way's own.
 * @param[out] buf        The answer.
 * @param[in]  cap        Bytes `buf` holds.
 * @param[out] len        The answer's bytes.
 * @return NULL with the answer; its error: ERR_REMOTE{board, seq} over that board's chain, or ERR_SESSION_TIMEOUT /
 *         _PEER_DOWN; ERR_CMD_BAD_LEN (longer than `cap`), ERR_SESSION_WOULD_BLOCK (to this board on a router task), or
 *         what sys_session_call returns.
 */
MUST_USE err_h sys_session_call_wait(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, void* buf,
                                     uint32_t cap, uint32_t* len);

/* ----------------------------------------------------------------------------
 * Push frames
 * ------------------------------------------------------------------------- */

/**
 * @brief Sends one PUSH frame to a peer: a stream's message, or a session channel's frame (a watch's entry, a remote
 * device's events). NOWAIT and SPARE on the session's provider: never blocks, any task, and never takes the room an
 * answer needs. Its seq is the session's counter of (`peer`, class), counted also when the provider refuses it.
 * @param[in] peer  A board or client id.
 * @param[in] data  [stream id or SYS_SESSION_CHANNEL_*][payload], at most CMD_DATA_MAX.
 * @param[in] len   Bytes in `data`.
 * @param[in] flags ROUTER_FLAG_URGENT (to the front of the provider's queue, and the urgent class's counter), or 0.
 * @return NULL when queued; ERR_CMD_BAD_ARG, ERR_SESSION_NO_ROUTE, or the provider's ERR_PROV_BUSY / _DOWN / _TOO_LONG.
 */
MUST_USE err_h sys_session_push(uint8_t peer, const uint8_t* data, uint32_t len, uint8_t flags);

/**
 * @brief Sends data to a provider of another board, or to a receiver of a client (SYS_SESSION.MD "Data to a provider").
 * The board it reaches sends the data on that provider as it is, without a header. NOWAIT and SPARE, never blocks; a
 * PUSH frame, counted as sys_session_push's.
 * @param[in] peer  A board or client id.
 * @param[in] to    The provider's id on `peer`.
 * @param[in] from  The sender's own id: a provider or a stream of this board.
 * @param[in] data  The data, at most CMD_DATA_MAX - 3.
 * @param[in] len   Bytes in `data`.
 * @param[in] flags ROUTER_FLAG_URGENT, or 0.
 * @return NULL when queued; ERR_CMD_BAD_ARG, ERR_CMD_BAD_LEN, ERR_SESSION_NO_ROUTE, or the provider's ERR_PROV_*.
 */
MUST_USE err_h sys_session_data(uint8_t peer, uint8_t to, uint8_t from, const uint8_t* data, uint32_t len,
                                uint8_t flags);

/* ----------------------------------------------------------------------------
 * Listeners (boot only)
 * ------------------------------------------------------------------------- */

/**
 * @brief Adds a listener of gone peers (SYS_SESSION.MD "Gone peers"): the streams, the watches, the dead-man.
 * @param[in] fn The listener.
 * @return NULL when added; ERR_SESSION_FULL (every CONFIG_SYS_SESSION_PEER_LISTENERS slot taken).
 */
MUST_USE err_h sys_session_on_gone(sys_session_gone_f fn);

/**
 * @brief Adds a listener of reached peers: the time broadcast, the watches' client, the remote devices.
 * @param[in] fn The listener.
 * @return NULL when added; ERR_SESSION_FULL (every CONFIG_SYS_SESSION_PEER_LISTENERS slot taken).
 */
MUST_USE err_h sys_session_on_reached(sys_session_reached_f fn);

/**
 * @brief Adds a listener of the PUSH frames for this board; each takes its own by the first byte (a channel's owner
 * its SYS_SESSION_CHANNEL_*).
 * @param[in] fn The listener.
 * @return NULL when added; ERR_SESSION_FULL (every CONFIG_SYS_SESSION_PUSH_LISTENERS slot taken).
 */
MUST_USE err_h sys_session_on_push(sys_session_push_f fn);

#ifdef __cplusplus
}
#endif
