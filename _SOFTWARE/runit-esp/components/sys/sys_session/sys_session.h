#pragma once

#include "cmd.h"
#include "errors.h"
#include "router.h"
#include "sys_data_provider.h"
#include "sys_session_config.h"
#include "sys_session_errors.h"

/**
 * @file sys_session.h
 * @brief Sessions (SYS_SESSION.MD): a board's way to its neighbours and the clients. A session is a framed provider
 * attached to a peer (parent, child, client); the session layer hands the commands for this board to the router,
 * forwards the frames for other boards on the driver's RX task, carries the streams to subscribed peers, makes the
 * board's calls (to another board or to itself) and tells who is gone. It has no task and no queue of its own. Code
 * knows a peer by its id: a board id or a client id.
 * @usage ERR_TRY(sys_session_attach(uart2, SYS_SESSION_CHILD, SYS_SESSION_FULL));
 *        ERR_TRY(sys_session_call(1, req, 0, 0, on_answer, ctx));
 */

#ifdef __cplusplus
extern "C" {
#endif

/** A gone listener's `via` for a peer named by the root's GONE: no session of this board ended. */
#define SYS_SESSION_VIA_NOTICE 0xFFu

/**
 * @brief A peer can no longer be reached. On the context that noticed it (a driver's RX task, the session's timer, the
 * caller of sys_session_detach): drop what you keep for it and return; never block.
 * @param peer  Its board or client id; SYS_SESSION_ID_BROADCAST with `via` PARENT: every peer outside this board's
 *              subtree (keep only those sys_session_below() holds).
 * @param via   sys_session_peer_e of this board's session that ended (PARENT or CLIENT is what the dead-man answers),
 *              or SYS_SESSION_VIA_NOTICE.
 */
typedef void (*sys_session_gone_f)(uint8_t peer, uint8_t via);

/**
 * @brief A peer can be reached: its session opened, or a child's hello names it. On the context that noticed it; never
 * block.
 * @param peer  Its board or client id.
 */
typedef void (*sys_session_reached_f)(uint8_t peer);

/**
 * @brief A stream frame for this board, on the driver's RX task. Copy what you keep and return; never block.
 * @param src   The board that sent it.
 * @param seq   Its stream's counter: a gap is frames lost on the way.
 * @param data  [connector][payload], valid for the call only.
 * @param len   Bytes in `data`.
 */
typedef void (*sys_session_stream_f)(uint8_t src, uint16_t seq, const uint8_t* data, uint32_t len);

/**
 * @brief Starts the session layer: this board's id (NVS, set by the app with ID; SYS_SESSION_ID_DEFAULT on a new
 * board), its boot id and its timer (pings, session timeouts). Boot step, after router_init(), before any attach.
 * @return NULL when started; the timer's error.
 */
MUST_USE err_h sys_session_init(void);

/** @brief This board's id. */
uint8_t sys_session_board_id(void);

/** @brief Whether this board has no parent: it is the root, the board the app talks to. Any task. */
bool sys_session_is_root(void);

/**
 * @brief Keeps a new board id in NVS: it is this board's id from the next start (ID).
 * @param board_id  0..SYS_SESSION_ID_LAST.
 * @return NULL when kept; ERR_CMD_BAD_ARG, the NVS error.
 */
MUST_USE err_h sys_session_set_board_id(uint8_t board_id);

/**
 * @brief Makes a provider the way to a peer: opens it and, when it is up, sends the hello. Until the peer's hello
 * comes, only control frames pass.
 * @param provider  A framed provider (SYS_DATA_PROVIDER_STATE_FRAMED).
 * @param peer      sys_session_peer_e.
 * @param access    sys_session_access_e: what the peer's commands may do.
 * @return NULL when attached; ERR_SESSION_NOT_FRAMED, ERR_SESSION_PARENT_TAKEN (a second parent), ERR_SESSION_CLIENT
 * (a client on a board with a parent, or a parent on a board with clients), ERR_SESSION_FULL, ERR_CMD_BAD_ARG
 * (already attached, a bad peer or access), or the provider's open error (ERR_PROV_NOT_FOUND, _IN_USE).
 */
MUST_USE err_h sys_session_attach(sys_data_provider_id_t provider, uint8_t peer, uint8_t access);

/**
 * @brief Ends the session on a provider and closes it: its peer and the boards behind it are gone.
 * @param provider  The provider.
 * @return NULL when detached or not attached; the provider's close error.
 */
MUST_USE err_h sys_session_detach(sys_data_provider_id_t provider);

/**
 * @brief Sets how long this board waits in silence on its parent's and its clients' sessions before it ends them: the
 * dead-man's time (sys_system's DEADMAN). Sends a hello on each, so the peer sends at least every third of it.
 * Children's sessions keep CONFIG_SYS_SESSION_TIMEOUT_MS. Any task.
 * @param ms  SYS_SESSION_TIMEOUT_MIN_MS..SYS_SESSION_TIMEOUT_MAX_MS; CONFIG_SYS_SESSION_TIMEOUT_MS at boot.
 * @return NULL when set; ERR_CMD_BAD_ARG.
 */
MUST_USE err_h sys_session_set_timeout(uint32_t ms);

#define SYS_SESSION_TIMEOUT_MIN_MS 150u
#define SYS_SESSION_TIMEOUT_MAX_MS 60000u

/**
 * @brief Runs a command on a board, this one too, and calls `cb` with its answer as the router does for a command it
 * ran: one way to run a command with its answer, here or there. Never blocks, any task (a device op too).
 *  - To this board (its id or SYS_SESSION_ID_SELF): router_inject(); the answer on the router task, no timeout.
 *  - To another: the request goes out (src this board, NOWAIT on the first hop) and a call slot keeps it until the
 *    answer, on the RX task it came on, which ends the call. An ERROR's record has ERR_REMOTE{board, seq} before
 *    that board's chain. No answer in the timeout: ERROR with this board's record of ERR_SESSION_TIMEOUT; the
 *    session it went through ended: of ERR_SESSION_PEER_DOWN (both on the session's timer or the RX task).
 * Every call to another board is answered (SYS_SESSION.MD "Answers"): NOREPLY goes to a broadcast only. A broadcast
 * from a board with a parent goes up to the root, which runs it and sends it down to every board; from the root it is
 * only sent down: the root has done what it broadcasts. `cb`'s `data`
 * has CMD_HEAD writable bytes in front of it, as the router's answers have.
 * `cb`'s cmd is the call: its request (the first CONFIG_ERRORS_RECORD_REQ_MAX bytes), its seq, src this board.
 * @param dst         A board (0..SYS_SESSION_ID_LAST), this one included, or SYS_SESSION_ID_BROADCAST (NOREPLY only).
 * @param req         The command: [key][dir][code | id + op][args], at most CMD_DATA_MAX.
 * @param flags       ROUTER_FLAG_URGENT; ROUTER_FLAG_NOREPLY with SYS_SESSION_ID_BROADCAST only (`cb` NULL).
 * @param timeout_ms  Without an answer this long: ERR_SESSION_TIMEOUT; 0: the way's own (the wired one to the peer
 *                    of a WIRED session, else CONFIG_SYS_SESSION_CALL_TIMEOUT_MS).
 * @param cb          The answer (router_done_f; its return is reported); NULL with NOREPLY.
 * @param ctx         Given to `cb`.
 * @return NULL when sent; ERR_CMD_BAD_ARG (NOREPLY to one board), ERR_SESSION_NO_ROUTE, ERR_SESSION_CALLS_FULL,
 * ERR_SESSION_BUSY (CONFIG_SYS_SESSION_PEER_IN_FLIGHT to that board: later), the first hop's ERR_PROV_BUSY / _DOWN /
 * _TOO_LONG, or (to this board) the router's ERR_ROUTER_QUEUE_FULL / _FRAME_LEN.
 */
MUST_USE err_h sys_session_call(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, router_done_f cb,
                                void* ctx);

/**
 * @brief sys_session_call, waited for: the caller's task sleeps until the call ends (SYS_SESSION.MD "Waiting for the
 * answer"). For configuration (the board config, the flasher) over any way, and for a blocking remote device to a
 * WIRED neighbour only. Not to this board on a router task; not on a driver's RX task, the connector task, the
 * session's timer or a task that called sys_session_no_wait().
 * @param dst         A board, this one included (not SYS_SESSION_ID_BROADCAST).
 * @param req         The command: [key][dir][code | id + op][args], at most CMD_DATA_MAX.
 * @param flags       ROUTER_FLAG_URGENT, or 0.
 * @param timeout_ms  As sys_session_call; 0: the way's own.
 * @param buf         Out: the answer.
 * @param cap         Bytes `buf` holds.
 * @param len         Out: the answer's bytes.
 * @return NULL with the answer; its error: ERR_REMOTE{board, seq} over that board's chain, or ERR_SESSION_TIMEOUT /
 * _PEER_DOWN; ERR_CMD_BAD_LEN (longer than `cap`), ERR_SESSION_WOULD_BLOCK (a task that must not wait), or what
 * sys_session_call returns.
 */
MUST_USE err_h sys_session_call_wait(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, void* buf,
                                     uint32_t cap, uint32_t* len);

/**
 * @brief Marks the calling task as one that must not wait for a call (sys_session_call_wait refuses it): the task
 * whose work an answer depends on, or that other answers wait behind. The driver RX tasks and the session's timer
 * are marked by the session itself. Call it at the start of the task.
 */
void sys_session_no_wait(void);

/**
 * @brief Whether a peer is the peer of an open session on a WIRED provider: a neighbour on a cable, where the VM may
 * wait for an answer. Any task.
 * @param peer  A board id.
 * @return true when wired and open.
 */
bool sys_session_wired(uint8_t peer);

/**
 * @brief Sends one stream frame to a peer: what a connector sends to its subscribers, and a watch to its owner
 * (SYS_SESSION.MD "Streams"). NOWAIT on the first hop: never blocks, any task.
 * @param peer   A board or client id.
 * @param seq    The connector's frame counter.
 * @param data   [connector][payload], at most CMD_DATA_MAX.
 * @param len    Bytes in `data`.
 * @param flags  ROUTER_FLAG_URGENT (to the front of the provider's queue), or 0.
 * @return NULL when queued; ERR_SESSION_NO_ROUTE, or the provider's ERR_PROV_BUSY / _DOWN / _TOO_LONG.
 */
MUST_USE err_h sys_session_stream(uint8_t peer, uint16_t seq, const uint8_t* data, uint32_t len, uint8_t flags);

/**
 * @brief Whether an open session of this board other than its parent's leads to a peer: a child's subtree, a client.
 * What a gone listener keeps when the parent's session ended. Any task.
 * @param peer  A board or client id.
 * @return true when below this board.
 */
bool sys_session_below(uint8_t peer);

/**
 * @brief Adds a listener of gone peers (SYS_SESSION.MD "Gone peers"): the connectors, the watches, the dead-man.
 * Boot only.
 * @param fn  The listener.
 * @return NULL when added; ERR_SESSION_FULL (every CONFIG_SYS_SESSION_GONE_LISTENERS slot taken).
 */
MUST_USE err_h sys_session_on_gone(sys_session_gone_f fn);

/**
 * @brief Adds a listener of reached peers: the time broadcast, later the connectors' mirrors. Boot only.
 * @param fn  The listener.
 * @return NULL when added; ERR_SESSION_FULL (every CONFIG_SYS_SESSION_GONE_LISTENERS slot taken).
 */
MUST_USE err_h sys_session_on_reached(sys_session_reached_f fn);

/**
 * @brief Adds a listener of the stream frames for this board; each takes its own by the first byte. Boot only.
 * @param fn  The listener.
 * @return NULL when added; ERR_SESSION_FULL (every CONFIG_SYS_SESSION_STREAM_LISTENERS slot taken).
 */
MUST_USE err_h sys_session_on_stream(sys_session_stream_f fn);

#ifdef __cplusplus
}
#endif
