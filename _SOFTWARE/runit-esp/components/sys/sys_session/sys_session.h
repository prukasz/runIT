#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cmd.h"
#include "data_matrix.h"
#include "errors.h"
#include "router_queue.h"
#include "sys_session_errors.h"

/**
 * @file sys_session.h
 * @brief Sessions (SYS_SESSION.MD): a session is the peer of a framed provider, a board or a client, known by its
 * hello. It is ports of the data matrix: its port, linked both ways with its provider, and a sub-port per module it
 * shows (MODULE_ID_ROUTER: the commands; a stream's module). A frame from the line goes out of the sub-port of its
 * module (a REQUEST or an ANSWER: MODULE_ID_ROUTER, a PUSH: its key); what a sub-port takes goes onto the line with
 * this board's id as src when it has none and, on a PUSH, its seq. Its own are the hello, the ping, NO_SESSION and the
 * dead-man. No task, no queue: the sender's task and the board timers' task.
 * @usage ERR_TRY(sys_session_attach(provider, 0, 0, (const uint8_t[]){MODULE_ID_ROUTER}, 1));
 *        ERR_TRY(sys_session_port(peer, MODULE_ID_ROUTER, &port));
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * Wire format, the one place it is assigned (never renumber): the control ops (published to the app by
 * templates/generate_enums.py), HELLO's body. The header of every frame, its type byte, kinds and sources are cmd.h's.
 *
 *   control  [hdr CONTROL, src, MODULE_ID_SESSION, 0][op][data]   HELLO: sys_session_hello_t
 * ------------------------------------------------------------------------- */

/** @brief A control frame's op: the first byte after its header. */
//#ref-enum @alias Control op
typedef enum {
  SYS_SESSION_HELLO = 1,       //@alias Hello @description Who I am (src, sys_session_hello_t): opens or updates a session
  SYS_SESSION_PING = 2,        //@alias Ping @description Nothing else to send: the session is alive
  SYS_SESSION_NO_SESSION = 3,  //@alias No session @description A frame on a session that is not open here: send your hello again
} sys_session_control_e;

/** @brief HELLO's body: who a board (or client) is, both ways when a session opens; its id is the header's src. */
typedef struct __attribute__((packed)) {
  uint32_t boot_id;  /**< Random per boot: a new one ends the session first (the peer restarted) */
  uint32_t build_id; /**< The firmware's build: the first 4 bytes of its ELF's SHA-256; 0 from a client */
} sys_session_hello_t;

_Static_assert(sizeof(sys_session_hello_t) == 8, "hello");

/* ----------------------------------------------------------------------------
 * Ports, the dead-man
 * ------------------------------------------------------------------------- */

/** @brief A session's port: its own (`module` 0, linked with its provider) or its sub-port of `module`. */
#define SYS_SESSION_PORT(provider, module) DATA_MATRIX_PORT(DATA_MATRIX_PORT_SESSION | (provider), (module))

/* The dead-man's time of a session: the silence that ends it, and a third of it the keepalive. */
#define SYS_SESSION_DEADMAN_MIN_MS 150u
#define SYS_SESSION_DEADMAN_MAX_MS 60000u

/** ATTACH's flag: the end of the session is a safety event (sys_system enters the safe state); else it only ends. */
//#ref-defines session_flag @alias Session flags @prefix SYS_SESSION_
#define SYS_SESSION_SAFETY (1u << 0)  //@alias Safety @description The end of the session enters the safe state

/* ----------------------------------------------------------------------------
 * Start
 * ------------------------------------------------------------------------- */

/**
 * @brief Starts the session layer: its hello (this board's boot id and build, board_id.h), its timer (pings, hellos,
 * the dead-man) and its listeners of the providers' events. Boot, after board_id_init, the data matrix and the events.
 * @return NULL when started.
 */
MUST_USE err_h sys_session_init(void);

/* ----------------------------------------------------------------------------
 * Sessions
 * ------------------------------------------------------------------------- */

/**
 * @brief Makes a provider's peer a session: registers its port and its sub-ports and links its port with the provider
 * both ways; when the provider is up, sends the hello. Until the peer's hello comes, only control frames pass. The
 * same again is done (a stored record run at every start).
 * @param[in] provider   A framed provider (SYS_DATA_PROVIDER_STATE_FRAMED), id below 0x80.
 * @param[in] deadman_ms 0: no dead-man, the session never ends by silence; else its time,
 *                       SYS_SESSION_DEADMAN_MIN_MS..SYS_SESSION_DEADMAN_MAX_MS, and the keepalive a third of it.
 * @param[in] flags      SYS_SESSION_SAFETY: the end of the session is a safety event (GONE's `safety`).
 * @param[in] modules    The modules of its sub-ports (MODULE_ID_ROUTER for the commands), at most
 *                       CONFIG_SYS_SESSION_PORTS.
 * @param[in] n          How many.
 * @return NULL when attached; ERR_SESSION_FULL, ERR_SESSION_NOT_FRAMED (none, or a raw provider), ERR_CMD_BAD_ARG (a
 *         bad time, flag or list, or attached with another), or the data matrix's error.
 */
MUST_USE err_h sys_session_attach(uint8_t provider, uint32_t deadman_ms, uint8_t flags, const uint8_t* modules,
                                  uint32_t n);

/**
 * @brief Ends the session on a provider and takes its ports out of the data matrix (their links go with them).
 * @param[in] provider The provider.
 * @return NULL, attached or not.
 */
MUST_USE err_h sys_session_detach(uint8_t provider);

/**
 * @brief A port of the open session with `peer`: its sub-port of `module` (MODULE_ID_ROUTER: where this board's calls
 * to `peer` go with router_request; a stream's module).
 * @param[in]  peer   A peer's id
 * @param[in]  module A module's number
 * @param[out] port   The port
 * @return NULL; ERR_SESSION_NO_ROUTE{dst} when no open session has that peer or that sub-port
 */
MUST_USE err_h sys_session_port(uint8_t peer, uint8_t module, data_matrix_port_id_t* port);

/**
 * @brief A call of this board to `peer`: router_request to its session's command sub-port (sys_session_port of
 * MODULE_ID_ROUTER). Never blocks.
 * @return NULL when sent (the answer comes to `cb`); ERR_SESSION_NO_ROUTE{dst}, or router_request's error.
 */
MUST_USE err_h sys_session_request(uint8_t peer, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, router_done_f cb,
                                   void* ctx);

/**
 * @brief sys_session_request that waits for the answer: router_request_wait to `peer`'s command sub-port.
 * @return NULL with the answer in `buf` (`*len`); ERR_SESSION_NO_ROUTE{dst}, or router_request_wait's error.
 */
MUST_USE err_h sys_session_request_wait(uint8_t peer, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, void* buf,
                                        uint32_t cap, uint32_t* len);

/**
 * @brief The peer of the open session a port belongs to (its port or a sub-port: a packet's `from`).
 * @param[in]  port The port
 * @param[out] peer Its id
 * @return false when the port is no open session's
 */
bool sys_session_peer(data_matrix_port_id_t port, uint8_t* peer);

/**
 * @brief Whether the open session with `board` runs on a wired provider (SYS_DATA_PROVIDER_STATE_WIRED). Any task.
 * @param[in] board A board
 * @return true when there is a session and it is wired
 */
bool sys_session_wired(uint8_t board);

/**
 * @brief Sets the dead-man of the open session with `peer`: `ms` 0 turns it off, else it is the silence limit and a
 * third of it the keepalive. Any task.
 * @param[in] peer   A peer's id
 * @param[in] ms     0, or SYS_SESSION_DEADMAN_MIN_MS..SYS_SESSION_DEADMAN_MAX_MS
 * @param[in] safety Whether its end is a safety event (SYS_SESSION_SAFETY)
 * @return NULL when set; ERR_CMD_BAD_ARG, ERR_SESSION_NO_ROUTE (no open session with `peer`)
 */
MUST_USE err_h sys_session_deadman_set(uint8_t peer, uint32_t ms, bool safety);

/** @brief The dead-man's time of the open session with `peer` in ms; 0: off, or no such session. */
uint32_t sys_session_deadman_get(uint8_t peer);

/**
 * @brief Sends a command on every open session (STOP, the time), never answered (NOREPLY). The board that makes it
 * does not run it: it has done what it broadcasts. NOWAIT, any task.
 * @param[in] req   The command: [key][dir][code | id + op][args], 2..CMD_DATA_MAX + 1 bytes.
 * @param[in] flags ROUTER_FLAG_URGENT, or 0.
 * @return NULL; ERR_CMD_BAD_ARG.
 */
MUST_USE err_h sys_session_broadcast(cmd_in_t req, uint8_t flags);

#ifdef __cplusplus
}
#endif
