#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cmd.h"
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
 *        ERR_TRY(sys_session_call(1, req, 0, 0, on_answer, ctx));
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * Wire format, the one place it is assigned (never renumber): the ids, the frame byte and kinds, the channels, the
 * control ops (published to the app by templates/generate_enums.py), then the frames.
 * ------------------------------------------------------------------------- */

/* Ids: every peer says its own in its hello and no two open sessions have the same. A board's is its place in the
   project (a command sets it, NVS keeps it); a client (the app, a test host) picks its own in the clients' range. */
//#ref-defines session_id @alias Peer ids @prefix SYS_SESSION_ID_
#define SYS_SESSION_ID_DEFAULT      0x00u  //@alias Default @description A board's id until one is set
#define SYS_SESSION_ID_LAST         0xEFu  //@alias Last board @description Boards are 0x00..0xEF
#define SYS_SESSION_ID_CLIENT_FIRST 0xF0u  //@alias First client @description Clients (the app, a test host) are 0xF0..0xFC; each picks its own
#define SYS_SESSION_ID_CLIENT_LAST  0xFCu  //@alias Last client @description The last client id
#define SYS_SESSION_ID_NONE         0xFDu  //@alias None @description A peer before its hello: no id yet
#define SYS_SESSION_ID_SELF         0xFEu  //@alias This board @description A command's src when the board runs its own (ROUTER_SRC_SELF); never on the wire
#define SYS_SESSION_ID_BROADCAST    0xFFu  //@alias Every peer @description A listener's mark for every peer; on no frame

/* The first byte of every frame: bits 0-1 the router's own flags (ROUTER_FLAG_URGENT on a request, its answer and a
   PUSH; _NOREPLY on a request), so a command's flags are this byte & SYS_SESSION_FRAME_FLAGS; bit 2 an answer's status
   (router_status_e); bits 3-5 zero; bits 6-7 the kind. */
//#ref-defines session_frame @alias Frame byte @prefix SYS_SESSION_FRAME_
#define SYS_SESSION_FRAME_FLAGS     (3u << 0)  //@alias Flags @description Bits 0-1: ROUTER_FLAG_URGENT, ROUTER_FLAG_NOREPLY
#define SYS_SESSION_FRAME_STATUS    (1u << 2)  //@alias Status @description Bit 2: an answer's router_status_e (OK, ERROR)
#define SYS_SESSION_FRAME_KIND      (3u << 6)  //@alias Kind @description Bits 6-7: sys_session_kind_e

/** @brief What a frame carries: (kind << 6) in its first byte. */
//#ref-enum @alias Frame kind
typedef enum {
  SYS_SESSION_REQUEST = 0,  //@alias Request @description A command for the peer; its answer comes back with the same seq
  SYS_SESSION_ANSWER = 1,   //@alias Answer @description The answer to the request seq: its status in the frame byte, then data
  SYS_SESSION_PUSH = 2,     //@alias Push @description [stream id | channel][payload], never answered: a stream's message, a channel's frame
  SYS_SESSION_CONTROL = 3,  //@alias Control @description The session's own: hello, ping, no session
} sys_session_kind_e;

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

/* A session's frames (SYS_SESSION.MD "Frames"), packed, little-endian: one 3-byte header on every frame. The provider's
 * driver takes one flat frame (`cmd_in_t`): this board's own are joined from header and body at the send.
 *
 *   request  [hdr REQUEST, flags ][key][dir][code | id + op][args]
 *   answer   [hdr ANSWER, status ][data]                 data: the answer, or the error record (ERROR)
 *   push     [hdr PUSH, flags    ][channel or stream id][payload]
 *   control  [hdr CONTROL        ][op][data]             HELLO: sys_session_hello_t
 */

#define SYS_SESSION_HDR_BYTES        ((uint32_t)sizeof(sys_session_hdr_t))
#define SYS_SESSION_FRAME(kind)      ((uint8_t)((kind) << 6))
#define SYS_SESSION_FRAME_KIND_OF(f) ((uint8_t)(((f) & SYS_SESSION_FRAME_KIND) >> 6))

/** @brief The header of every frame: CMD_HEAD bytes. */
typedef struct __attribute__((packed)) {
  uint8_t frame;  /**< Bits 0-1: ROUTER_FLAG_*; bit 2: an answer's status; bits 6-7: kind */
  uint16_t seq;   /**< Request: the sender's counter; answer: echoed; push: the sender's count of its class; else 0 */
} sys_session_hdr_t;

_Static_assert(sizeof(sys_session_hdr_t) == CMD_HEAD, "the header is CMD_HEAD");

/** @brief HELLO's body: who a board (or client) is; both ways when a session opens. */
typedef struct __attribute__((packed)) {
  uint8_t id;        /**< Its own: a board's place, or a client's pick */
  uint32_t boot_id;  /**< Random per boot: a new one ends the session first (the peer restarted) */
  uint32_t build_id; /**< The firmware's build: the first 4 bytes of its ELF's SHA-256; 0 from a client */
} sys_session_hello_t;

_Static_assert(sizeof(sys_session_hello_t) == 9, "hello");

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
 * @brief Starts the session layer: this board's id (NVS, set by the app with ID; SYS_SESSION_ID_DEFAULT on a new
 * board), its boot id and its timer (pings, session and call timeouts, on the board timers' task). Boot, after the NVS
 * partitions and router_init(), before any attach.
 * @return NULL when started.
 */
MUST_USE err_h sys_session_init(void);

/** @brief This board's id. */
uint8_t sys_session_get_board_id(void);

/**
 * @brief The provider of the open session with `peer`.
 * @param[in]  peer     A peer's id
 * @param[out] provider The provider
 * @return false when no open session has that peer
 */
bool sys_session_route(uint8_t peer, uint8_t* provider);

/** @brief The peer of the open session on `provider`; SYS_SESSION_ID_NONE when it has none or it is not open. */
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
 * @brief Runs a command on a board, this one too, and calls `cb` with its answer as the router does for a command it
 * ran: one way to run a command with its answer, here or there. Never blocks, any task (a device op too).
 *  - To this board (its id or SYS_SESSION_ID_SELF): router_inject(); the answer on the router task, no timeout.
 *  - To another: the request goes out on the open session whose peer is `dst` (NOWAIT) and a call slot keeps it until
 *    the answer, on the RX task it came on, which ends the call. An ERROR's record has ERR_REMOTE{board, seq} before
 *    that board's chain. No answer in the timeout: ERROR with this board's record of ERR_SESSION_TIMEOUT; the session
 *    it went through ended: of ERR_SESSION_PEER_DOWN (both on the board timers' task or the RX task).
 * Every call to another board is answered. `cb`'s cmd is the call: its request (the first
 * CONFIG_ERRORS_RECORD_REQ_MAX bytes), its seq, src this board.
 * @param[in] dst        A peer's id, this board's included.
 * @param[in] req        The command: [key][dir][code | id + op][args], at most CMD_DATA_MAX.
 * @param[in] flags      ROUTER_FLAG_URGENT, or 0.
 * @param[in] timeout_ms Without an answer this long: ERR_SESSION_TIMEOUT; 0: CONFIG_SYS_SESSION_CALL_TIMEOUT_MS.
 * @param[in] cb         The answer (router_done_f; its return is reported); NULL: nobody wants it.
 * @param[in] ctx        Given to `cb`.
 * @return NULL when sent; ERR_SESSION_NO_ROUTE, ERR_SESSION_CALLS_FULL, the provider's ERR_PROV_BUSY / _DOWN, or (to
 *         this board) the router's ERR_ROUTER_QUEUE_FULL / _FRAME_LEN.
 */
MUST_USE err_h sys_session_call(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, router_done_f cb,
                                void* ctx);

/**
 * @brief sys_session_call, waited for: the caller's task sleeps until the call ends, at most its timeout and a margin
 * of its own (SYS_SESSION.MD "Waiting for the answer"), so a wait never hangs, also on a task an answer or the timer
 * would need. For configuration (the flasher, a board's setup) and for a remote device. Not to this board on a router
 * task.
 * @param[in]  dst        A peer's id, this board's included.
 * @param[in]  req        The command: [key][dir][code | id + op][args], at most CMD_DATA_MAX.
 * @param[in]  flags      ROUTER_FLAG_URGENT, or 0.
 * @param[in]  timeout_ms As sys_session_call.
 * @param[out] buf        The answer.
 * @param[in]  cap        Bytes `buf` holds.
 * @param[out] len        The answer's bytes.
 * @return NULL with the answer; its error: ERR_REMOTE{board, seq} over that board's chain, or ERR_SESSION_TIMEOUT /
 *         _PEER_DOWN; ERR_CMD_BAD_LEN (longer than `cap`), ERR_SESSION_WOULD_BLOCK (to this board on a router task), or
 *         what sys_session_call returns.
 */
MUST_USE err_h sys_session_call_wait(uint8_t dst, cmd_in_t req, uint8_t flags, uint32_t timeout_ms, void* buf,
                                     uint32_t cap, uint32_t* len);

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
