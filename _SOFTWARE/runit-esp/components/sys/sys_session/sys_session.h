#pragma once

#include "cmd.h"
#include "errors.h"
#include "router.h"
#include "sys_data_provider.h"
#include "sys_session_config.h"
#include "sys_session_errors.h"

/**
 * @file sys_session.h
 * @brief Sessions (SYS_SESSION.MD): the board's way to its clients. A session is a framed provider attached to a peer;
 * the session layer hands the commands for this board to the router and frames their answers, carries the streams to
 * subscribed peers, opens and ends sessions (hello, ping, timeout) and tells who is gone. It has no task and no queue
 * of its own. Code knows a peer by its id: the client id the board gave it.
 * @usage ERR_TRY(sys_session_attach(SYS_UART_PROVIDER, SYS_SESSION_CLIENT, SYS_SESSION_FULL));
 *        ERR_TRY(sys_session_stream(peer, seq, frame, len, 0));
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A peer can no longer be reached: its session ended (silence past its timeout, the provider down, a new
 * boot_id, detach). On the context that noticed it (a driver's RX task, the session's timer, the caller of
 * sys_session_detach): drop what you keep for it and return; never block.
 * @param peer  Its client id.
 * @param via   sys_session_peer_e of the session that ended (CLIENT is what the dead-man answers).
 */
typedef void (*sys_session_gone_f)(uint8_t peer, uint8_t via);

/**
 * @brief A peer can be reached: its session opened. On the context that noticed it; never block.
 * @param peer  Its client id.
 */
typedef void (*sys_session_reached_f)(uint8_t peer);

/**
 * @brief A stream frame for this board, on the driver's RX task. Copy what you keep and return; never block.
 * @param src   The peer that sent it.
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

/**
 * @brief Keeps a new board id in NVS: it is this board's id from the next start (ID).
 * @param board_id  0..SYS_SESSION_ID_LAST.
 * @return NULL when kept; ERR_CMD_BAD_ARG, the NVS error.
 */
MUST_USE err_h sys_session_set_board_id(uint8_t board_id);

/**
 * @brief Makes a provider the way to a peer: opens it and, when it is up, sends the hello. Until the peer's hello
 * comes, only control frames pass. The board takes clients only (no parent, no children): it is the root.
 * @param provider  A framed provider (SYS_DATA_PROVIDER_STATE_FRAMED).
 * @param peer      sys_session_peer_e: SYS_SESSION_CLIENT.
 * @param access    sys_session_access_e: what the peer's commands may do.
 * @return NULL when attached; ERR_SESSION_NOT_FRAMED, ERR_SESSION_CLIENT (PARENT, CHILD), ERR_SESSION_FULL,
 * ERR_CMD_BAD_ARG (already attached, a bad access), or the provider's open error (ERR_PROV_NOT_FOUND, _IN_USE).
 */
MUST_USE err_h sys_session_attach(sys_data_provider_id_t provider, uint8_t peer, uint8_t access);

/**
 * @brief Ends the session on a provider and closes it: its peer is gone.
 * @param provider  The provider.
 * @return NULL when detached or not attached; the provider's close error.
 */
MUST_USE err_h sys_session_detach(sys_data_provider_id_t provider);

/**
 * @brief Sets how long this board waits in silence on its clients' sessions before it ends them: the dead-man's time
 * (sys_system's DEADMAN). Sends a hello on each open one, so the peer sends at least every third of it. Any task.
 * @param ms  SYS_SESSION_TIMEOUT_MIN_MS..SYS_SESSION_TIMEOUT_MAX_MS; CONFIG_SYS_SESSION_TIMEOUT_MS at boot.
 * @return NULL when set; ERR_CMD_BAD_ARG.
 */
MUST_USE err_h sys_session_set_timeout(uint32_t ms);

#define SYS_SESSION_TIMEOUT_MIN_MS 150u
#define SYS_SESSION_TIMEOUT_MAX_MS 60000u

/**
 * @brief Runs a command on this board and calls `cb` with its answer as the router does for a command it ran:
 * router_inject(), the answer on the router task. Never blocks, any task.
 * @param dst    This board: its id or SYS_SESSION_ID_SELF; another board is ERR_SESSION_NO_ROUTE.
 * @param req    The command: [key][dir][code | id + op][args], at most CMD_DATA_MAX.
 * @param flags  ROUTER_FLAG_URGENT, ROUTER_FLAG_NOREPLY.
 * @param cb     The answer (router_done_f; its return is reported); NULL: none.
 * @param ctx    Given to `cb`.
 * @return NULL when queued; ERR_SESSION_NO_ROUTE, the router's ERR_ROUTER_QUEUE_FULL / _FRAME_LEN.
 */
MUST_USE err_h sys_session_call(uint8_t dst, cmd_in_t req, uint8_t flags, router_done_f cb, void* ctx);

/**
 * @brief Whether a peer is the peer of an open session on a WIRED provider: a neighbour on a cable. Any task.
 * @param peer  A peer id.
 * @return true when wired and open.
 */
bool sys_session_wired(uint8_t peer);

/**
 * @brief Sends one stream frame to a peer: what a connector sends to its subscribers (SYS_SESSION.MD "Streams").
 * NOWAIT: never blocks, any task.
 * @param peer   A peer id.
 * @param seq    The connector's frame counter.
 * @param data   [connector][payload], at most CMD_DATA_MAX.
 * @param len    Bytes in `data`.
 * @param flags  ROUTER_FLAG_URGENT (to the front of the provider's queue), or 0.
 * @return NULL when queued; ERR_SESSION_NO_ROUTE, or the provider's ERR_PROV_BUSY / _DOWN / _TOO_LONG.
 */
MUST_USE err_h sys_session_stream(uint8_t peer, uint16_t seq, const uint8_t* data, uint32_t len, uint8_t flags);

/**
 * @brief Whether an open session of this board other than its parent's leads to a peer: a client. Any task.
 * @param peer  A peer id.
 * @return true when below this board.
 */
bool sys_session_below(uint8_t peer);

/**
 * @brief Adds a listener of gone peers (SYS_SESSION.MD "Gone peers"): the connectors, the dead-man. Boot only.
 * @param fn  The listener.
 * @return NULL when added; ERR_SESSION_FULL (every CONFIG_SYS_SESSION_GONE_LISTENERS slot taken).
 */
MUST_USE err_h sys_session_on_gone(sys_session_gone_f fn);

/**
 * @brief Adds a listener of reached peers. Boot only.
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
