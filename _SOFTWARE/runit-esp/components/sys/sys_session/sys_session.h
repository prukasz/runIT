#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cmd.h"
#include "data_matrix.h"
#include "errors.h"
#include "router_queue.h"
#include "sys_data_provider.h"
#include "sys_session_errors.h"

/**
 * @file sys_session.h
 * @brief Sessions (SYS_SESSION.MD): a session is a framed provider with one peer on it, a board or a client, set up with
 * the peer's hello and kept until the session is detached. It hands the commands of its peer to the router, carries
 * the board's own calls to it and its PUSH frames, and publishes who is reached and who is gone (sys_session_events.h). No routing: a frame goes
 * to the peer on the provider it came on, and nowhere else. It has no task and no queue of its own: the driver's RX
 * task and the board timers' task. Code knows a peer by its id, from its hello: a board id or a client id.
 * @usage ERR_TRY(sys_session_attach(provider, 0));
 *        ERR_TRY(sys_session_port(1, MODULE_ID_ROUTER, &port));
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * Wire format, the one place it is assigned (never renumber): the channels, the control ops (published to the app by
 * templates/generate_enums.py), then HELLO's body. The header of every frame, its type byte, kinds and sources are
 * cmd.h's (cmd_hdr_t, CMD_TYPE_*, cmd_kind_e, CMD_SRC_*).
 * ------------------------------------------------------------------------- */

/* The first byte of a PUSH frame's body: below SYS_SESSION_CHANNEL_FIRST a stream's id; from it a **channel**, which is
   a module's: its module number with bit 7 set, SYS_SESSION_CHANNEL(MODULE_ID_WATCH). So no list of channels is kept
   here and two modules can't share one. A module sends its frame to the one peer it names (sys_session_push) and takes
   the frames that reach this board (sys_session_on_push) by this byte. The session knows no channel. A module with a
   channel has a number under SYS_SESSION_CHANNEL_FIRST. */
//#ref-defines session_channel @alias Session channels @prefix SYS_SESSION_CHANNEL_
#define SYS_SESSION_CHANNEL_FIRST 0x80u  //@alias First channel @description From this byte a PUSH frame is a module's channel: the module's number with bit 7 set; below it a stream's id

#define SYS_SESSION_CHANNEL(module_id) (SYS_SESSION_CHANNEL_FIRST | (module_id))

/** @brief A control frame's op: the first byte after its header. */
//#ref-enum @alias Control op
typedef enum {
  SYS_SESSION_HELLO = 1,       //@alias Hello @description Who I am (sys_session_hello_t): opens or updates a session
  SYS_SESSION_PING = 2,        //@alias Ping @description Nothing else to send: the session is alive
  SYS_SESSION_NO_SESSION = 3,  //@alias No session @description A frame on a session that is not open here: send your hello again
} sys_session_control_e;

/* A session's frames (SYS_SESSION.MD "Frames"), after the header cmd_hdr_t (cmd.h):
 *
 *   request  [hdr REQUEST, flags, src, key, seq ][dir][code | id + op][args]
 *   answer   [hdr ANSWER, status, src, key, seq ][data]        data: the answer, or the error record (ERROR)
 *   push     [hdr PUSH, flags, src, key, seq    ][data]        key: the stream's module
 *   control  [hdr CONTROL, src, MODULE_ID_SESSION][op][data]  HELLO: sys_session_hello_t
 */

/** @brief HELLO's body: who a board (or client) is, both ways when a session opens; its id is the header's src. */
typedef struct __attribute__((packed)) {
  uint32_t boot_id;  /**< Random per boot: a new one ends the session first (the peer restarted) */
  uint32_t build_id; /**< The firmware's build: the first 4 bytes of its ELF's SHA-256; 0 from a client */
} sys_session_hello_t;

_Static_assert(sizeof(sys_session_hello_t) == 8, "hello");

/* ----------------------------------------------------------------------------
 * Constants and the listener type
 * ------------------------------------------------------------------------- */

/* The dead-man's time of a session: the silence that ends it, and a third of it the keepalive. */
#define SYS_SESSION_DEADMAN_MIN_MS 150u
#define SYS_SESSION_DEADMAN_MAX_MS 60000u

/** ATTACH's flag: the end of the session is a safety event (sys_system enters the safe state); else it only ends. */
//#ref-defines session_flag @alias Session flags @prefix SYS_SESSION_
#define SYS_SESSION_SAFETY (1u << 0)  //@alias Safety @description The end of the session enters the safe state

/**
 * @brief A PUSH frame for this board, on the driver's RX task. Copy what you keep and return; never block.
 * @param provider The provider the session with its sender runs on.
 * @param data     [tag][payload], valid for the call only.
 * @param len      Bytes in `data`.
 */
typedef void (*sys_session_push_f)(uint8_t provider, const uint8_t* data, uint32_t len);

/* ----------------------------------------------------------------------------
 * Start and this board
 * ------------------------------------------------------------------------- */

/**
 * @brief Starts the session layer: this board's id (NVS, set by the app with ID; CMD_SRC_NONE on a new
 * board), its boot id and its timer (pings, session and call timeouts, on the board timers' task). Boot, after the NVS
 * partitions and router_init(), before any attach.
 * @return NULL when started.
 */
MUST_USE err_h sys_session_init(void);

/** @brief This board's id. */
uint8_t sys_session_get_board_id(void);

/**
 * @brief A port of the open session with `peer`: its sub-port of `module` (MODULE_ID_ROUTER: its commands, where this
 * board's calls to `peer` go with router_request; a stream's module).
 * @param[in]  peer   A peer's id
 * @param[in]  module A module's number
 * @param[out] port   The port
 * @return NULL; ERR_SESSION_NO_ROUTE{dst} when no open session has that peer
 */
MUST_USE err_h sys_session_port(uint8_t peer, uint8_t module, data_matrix_port_id_t* port);

/** @brief The peer of the open session on `provider`; CMD_SRC_NONE when it has none or it is not open. */
uint8_t sys_session_peer(uint8_t provider);

/**
 * @brief Whether a call to `board` leaves on a wired provider now (its session's provider has SYS_DATA_PROVIDER_STATE_WIRED).
 * Any task, no wait.
 * @param[in] board A board
 * @return true when there is a session and it is wired
 */
bool sys_session_wired(uint8_t board);

/* ----------------------------------------------------------------------------
 * Sessions
 * ------------------------------------------------------------------------- */

/**
 * @brief Makes a provider a session: opens it and, when it is up, sends the hello. Until the peer's hello comes, only
 * control frames pass. The session stays on the provider until sys_session_detach, whoever is or is not on it.
 * The same again on an attached provider is done (a stored record run at every start).
 * @param[in] provider   A framed provider (SYS_DATA_PROVIDER_STATE_FRAMED).
 * @param[in] deadman_ms 0: no dead-man, the session never ends by silence; else its time,
 *                       SYS_SESSION_DEADMAN_MIN_MS..SYS_SESSION_DEADMAN_MAX_MS: the silence that ends the session (it
 *                       stays on the provider, empty, until the next hello) and the board's keepalive is a third of it.
 * @param[in] flags      SYS_SESSION_SAFETY: the end of the session is a safety event (gone's `safety`).
 * @return NULL when attached; ERR_SESSION_FULL, ERR_SESSION_NOT_FRAMED (a raw provider), ERR_CMD_BAD_ARG (a bad time or
 *         flag, or attached with another), or the provider's open error (ERR_PROV_NOT_FOUND, _IN_USE).
 */
MUST_USE err_h sys_session_attach(uint8_t provider, uint32_t deadman_ms, uint8_t flags);

/**
 * @brief Ends the session on a provider and closes it: its peer is gone.
 * @param[in] provider The provider.
 * @return NULL when detached or not attached; the provider's close error.
 */
MUST_USE err_h sys_session_detach(uint8_t provider);

/**
 * @brief Sets the dead-man of the open session with `peer`: `ms` 0 turns it off (the session never ends by silence),
 * else it is the silence limit, and a third of it the keepalive both sides keep when both have it set. Any task.
 * @param[in] peer   A peer's id
 * @param[in] ms     0, or SYS_SESSION_DEADMAN_MIN_MS..SYS_SESSION_DEADMAN_MAX_MS
 * @param[in] safety Whether its end is a safety event (SYS_SESSION_SAFETY)
 * @return NULL when set; ERR_CMD_BAD_ARG, ERR_SESSION_NO_ROUTE (no open session with `peer`)
 */
MUST_USE err_h sys_session_deadman_set(uint8_t peer, uint32_t ms, bool safety);

/** @brief The dead-man's time of the open session with `peer` in ms; 0: off, or no such session. */
uint32_t sys_session_deadman_get(uint8_t peer);

/* ----------------------------------------------------------------------------
 * Calls
 * ------------------------------------------------------------------------- */

/**
 * @brief Sends a command on every open session (STOP, the time), never answered. The board that makes it does not run
 * it: it has done what it broadcasts. NOWAIT, any task.
 * @param[in] req   The command: [key][dir][code | id + op][args], at most CMD_DATA_MAX.
 * @param[in] flags ROUTER_FLAG_URGENT, or 0.
 * @return NULL when sent on every session; ERR_CMD_BAD_ARG, or the first provider's ERR_PROV_BUSY / _DOWN.
 */
MUST_USE err_h sys_session_broadcast(cmd_in_t req, uint8_t flags);

/* ----------------------------------------------------------------------------
 * Push frames
 * ------------------------------------------------------------------------- */

/**
 * @brief Sends one PUSH frame on the open session of a provider: a stream's message, or a module's channel frame (a
 * watch's entry, a remote device's events). NOWAIT and SPARE: never blocks, any task, and never takes the room an
 * answer needs. Its seq is the session's count of the PUSH frames of its class (urgent or not), counted also when the
 * provider refuses it: a gap at the receiver is frames lost. The data matrix's T end sends through it.
 * @param[in] provider The provider with the session.
 * @param[in] data     [tag][payload], 1..CMD_DATA_MAX bytes.
 * @param[in] flags    ROUTER_FLAG_URGENT (to the front of the provider's queue), or 0.
 * @return NULL when queued; ERR_CMD_BAD_ARG, ERR_CMD_BAD_LEN, ERR_SESSION_NOT_OPEN, or the provider's ERR_PROV_BUSY /
 *         _DOWN.
 */
MUST_USE err_h sys_session_push(uint8_t provider, cmd_in_t data, uint8_t flags);

/**
 * @brief Sets who takes the PUSH frames that reach this board: the data matrix, which sends each to the routes of its
 * tag (the first byte). Boot, once.
 * @param[in] fn The taker.
 */
void sys_session_set_push(sys_session_push_f fn);

#ifdef __cplusplus
}
#endif
