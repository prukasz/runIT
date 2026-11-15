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
 * @brief Sessions (SYS_SESSION.MD): a board's way to its neighbours and the clients. A session is a provider attached
 * as a way to a peer, a slave told at ATTACH or whoever the hello says (a client or this board's master); the session layer hands the commands for
 * this board to the router, forwards the frames for other boards on the driver's RX task, carries the PUSH frames,
 * makes the board's calls (to another board or to itself) and tells who is reached and who is gone. It has no task and
 * no queue of its own: the driver's RX task and the board timers' task. Code knows a peer by its id: a board id or a
 * client id.
 * @usage ERR_TRY(sys_session_attach(uart, SYS_SESSION_SLAVE));
 *        ERR_TRY(sys_session_call(1, req, 0, 0, on_answer, ctx));
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * Wire format, the one place it is assigned (never renumber): the ids, the frame byte and kinds, the channels, the
 * control ops, the ways and the roles (published to the app by templates/generate_enums.py), then the frames.
 * ------------------------------------------------------------------------- */

/* Ids: every peer says its own in its hello and two peers may not have one. A board's is its place in the project (a
   command sets it, NVS keeps it); a client (the app, a test host) picks its own in the clients' range. */
//#ref-defines session_id @alias Peer ids @prefix SYS_SESSION_ID_
#define SYS_SESSION_ID_DEFAULT      0x00u  //@alias Default @description A board's id until one is set
#define SYS_SESSION_ID_LAST         0xEFu  //@alias Last board @description Boards are 0x00..0xEF
#define SYS_SESSION_ID_CLIENT_FIRST 0xF0u  //@alias First client @description Clients (the app, a test host) are 0xF0..0xFC; each picks its own
#define SYS_SESSION_ID_CLIENT_LAST  0xFCu  //@alias Last client @description The last client id
#define SYS_SESSION_ID_NONE         0xFDu  //@alias None @description A peer before its hello: no id yet
#define SYS_SESSION_ID_SELF         0xFEu  //@alias This board @description A command's src when the board runs its own (ROUTER_SRC_SELF); never in a frame's header
#define SYS_SESSION_ID_BROADCAST    0xFFu  //@alias Every board @description Every board (STOP, time); never answered

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
  SYS_SESSION_REQUEST = 0,  //@alias Request @description A command for dst; its answer goes back to src
  SYS_SESSION_ANSWER = 1,   //@alias Answer @description The answer to the request src/seq: its status in the frame byte, then data
  SYS_SESSION_PUSH = 2,     //@alias Push @description [stream id | channel][payload], never answered: a stream's message, a channel's frame
  SYS_SESSION_CONTROL = 3,  //@alias Control @description The session's own: hello, ping, no session, peer
} sys_session_kind_e;

/* The first byte of a PUSH frame's body: below SYS_SESSION_CHANNEL_FIRST a stream's id; from it a **channel**, which is
   a module's: its module number with bit 7 set, SYS_SESSION_CHANNEL(MODULE_ID_WATCH). So no list of channels is kept
   here and two modules can't share one. A channel is no stream (no queue, no subscribers): a module sends its frame
   to the one peer it names (sys_session_push) and takes the frames that reach this board (sys_session_on_push) by this
   byte. The session knows its own channel only, DATA: the board it reaches sends the data on its provider. A module
   with a channel has a number under SYS_SESSION_CHANNEL_FIRST. */
//#ref-defines session_channel @alias Session channels @prefix SYS_SESSION_CHANNEL_
#define SYS_SESSION_CHANNEL_FIRST 0x80u  //@alias First channel @description From this byte a PUSH frame is a module's channel: the module's number with bit 7 set; below it a stream's id

#define SYS_SESSION_CHANNEL(module_id) (SYS_SESSION_CHANNEL_FIRST | (module_id))

/* DATA, the session's own channel: [to id][from id][data] (SYS_SESSION.MD "Data to a provider"). */
#define SYS_SESSION_CHANNEL_DATA SYS_SESSION_CHANNEL(MODULE_ID_SESSION)
_Static_assert(MODULE_ID_SESSION < SYS_SESSION_CHANNEL_FIRST, "a module with a channel has a number under 0x80");

/** @brief A control frame's op: the first byte after its header. */
//#ref-enum @alias Control op
typedef enum {
  SYS_SESSION_HELLO = 1,       //@alias Hello @description Who I am (sys_session_hello_t): opens or updates a session
  SYS_SESSION_PING = 2,        //@alias Ping @description Nothing else to send: the session is alive
  SYS_SESSION_NO_SESSION = 3,  //@alias No session @description A frame on a session that is not open here: send your hello again
  SYS_SESSION_PEER = 4,        //@alias Peer @description sys_session_peer_t: the root's session with that peer opened or ended; to every other peer
} sys_session_control_e;

/** @brief What the peer of a session is. ATTACH tells UNKNOWN or SLAVE; the rest is the peer's hello. */
//#ref-enum @alias Peer
typedef enum {
  SYS_SESSION_UNKNOWN = 0,  //@alias Unknown @description Not told: the hello says, master or client (the console, the runIT BLE pair); nothing before it
  SYS_SESSION_MASTER = 1,   //@alias Master @description This board's master: a board's hello on a way not told; then the board's only session
  SYS_SESSION_SLAVE = 2,    //@alias Slave @description A board of this one, told at ATTACH (a framed provider: a UART port, a BLE link made as a central)
  SYS_SESSION_CLIENT = 3,   //@alias Client @description A client's hello on a way not told (the app, a test host): on the root only
} sys_session_peer_e;

/* A session's frames (SYS_SESSION.MD "Frames"), packed, little-endian: one 5-byte header on every frame, so a frame is
 * routed by its dst without reading or changing its body. The provider's driver takes one flat frame (`cmd_in_t`): a
 * forwarded frame goes on as it came, this board's own are joined from header and body at the send.
 *
 *   request  [hdr REQUEST, flags ][key][dir][code | id + op][args]
 *   answer   [hdr ANSWER, status ][data]                 data: the answer, or the error record (ERROR)
 *   push     [hdr PUSH, flags    ][stream id][payload]   one stream's message to a subscribed peer, or a module's channel (from 0x80)
 *            [hdr PUSH, flags    ][DATA][to id][from id][data]   for the provider `to id` of dst
 *   control  [hdr CONTROL        ][op][data]             HELLO: sys_session_hello_t; PEER: sys_session_peer_t
 */

#define SYS_SESSION_HDR_BYTES        ((uint32_t)sizeof(sys_session_hdr_t))
#define SYS_SESSION_FRAME(kind)      ((uint8_t)((kind) << 6))
#define SYS_SESSION_FRAME_KIND_OF(f) ((uint8_t)(((f) & SYS_SESSION_FRAME_KIND) >> 6))

/** @brief The header of every frame: CMD_HEAD bytes. */
typedef struct __attribute__((packed)) {
  uint8_t frame;  /**< Bits 0-1: ROUTER_FLAG_*; bit 2: an answer's status; bits 6-7: kind */
  uint8_t dst;    /**< The board or client it goes to */
  uint8_t src;    /**< The board or client that made it; an answer goes back to it */
  uint16_t seq;   /**< Request: the source's counter; answer: echoed; push: src's count to dst of its class; else 0 */
} sys_session_hdr_t;

_Static_assert(sizeof(sys_session_hdr_t) == CMD_HEAD, "the header is CMD_HEAD");

/** @brief HELLO's body: who a board (or client) is; both ways when a session opens, again when its timeout changes. */
typedef struct __attribute__((packed)) {
  uint8_t id;           /**< Its own: a board's place, or a client's pick */
  uint32_t boot_id;     /**< Random per boot: a new one ends the session first (the peer restarted) */
  uint32_t build_id;    /**< The firmware's build: the first 4 bytes of its ELF's SHA-256; 0 from a client */
  uint16_t timeout_ms;  /**< It ends the session after this much silence: send it something every third of it */
} sys_session_hello_t;

_Static_assert(sizeof(sys_session_hello_t) == 11, "hello");

/** @brief PEER's body: the root's session with `id` opened (`up` 1) or ended (`up` 0). */
typedef struct __attribute__((packed)) {
  uint8_t id;  /**< The peer: a board or a client */
  uint8_t up;  /**< 1: reached; 0: gone */
} sys_session_peer_t;

_Static_assert(sizeof(sys_session_peer_t) == 2, "peer");

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
 * @param role  sys_session_peer_e of this board's session that ended (MASTER or CLIENT is what the dead-man answers),
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
 * @param data  [stream id or channel][payload], valid for the call only.
 * @param len   Bytes in `data`.
 */
typedef void (*sys_session_push_f)(uint8_t src, const uint8_t* data, uint32_t len);

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
 * @brief Whether a call to `board` leaves on a wired way now (its route's provider has SYS_DATA_PROVIDER_STATE_WIRED).
 * Any task, no wait.
 * @param[in] board A board
 * @return true when there is a route and it is wired
 */
bool sys_session_wired(uint8_t board);

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
 * @param[in] peer     sys_session_peer_e: SYS_SESSION_SLAVE (the provider leads to a slave) or SYS_SESSION_UNKNOWN (a client
 *                     or this board's master: the hello says which).
 * @return NULL when attached; ERR_SESSION_FULL, ERR_SESSION_NOT_FRAMED (a raw provider), ERR_CMD_BAD_ARG (attached as
 *         the other, a MASTER or CLIENT), or the provider's open error (ERR_PROV_NOT_FOUND, _IN_USE).
 */
MUST_USE err_h sys_session_attach(uint8_t provider, uint8_t peer);

/**
 * @brief Ends the session on a provider and closes it: its peer is gone.
 * @param[in] provider The provider.
 * @return NULL when detached or not attached; the provider's close error.
 */
MUST_USE err_h sys_session_detach(uint8_t provider);

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
 * Every call to another board is answered. `cb`'s cmd is the call: its request (the first
 * CONFIG_ERRORS_RECORD_REQ_MAX bytes), its seq, src this board.
 * @param[in] dst        A board (0..SYS_SESSION_ID_LAST), this one included.
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
 * @param[in]  dst        A board, this one included.
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
 * @brief Sends a command to every board (STOP, the time), never answered. A slave's goes to its master, which runs it
 * and sends it to every other slave; the root's goes to every slave. The board that makes it does not run it: it has
 * done what it broadcasts. NOWAIT, any task.
 * @param[in] req   The command: [key][dir][code | id + op][args], at most CMD_DATA_MAX.
 * @param[in] flags ROUTER_FLAG_URGENT, or 0.
 * @return NULL when sent on every way; ERR_CMD_BAD_ARG, or the first provider's ERR_PROV_BUSY / _DOWN.
 */
MUST_USE err_h sys_session_broadcast(cmd_in_t req, uint8_t flags);

/* ----------------------------------------------------------------------------
 * Push frames
 * ------------------------------------------------------------------------- */

/**
 * @brief Sends one PUSH frame to a peer: a stream's message, or a module's channel frame (a watch's entry, a remote
 * device's events). NOWAIT and SPARE on the session's provider: never blocks, any task, and never takes the room an
 * answer needs. Its seq is this board's count of the PUSH frames to `peer` of its class (urgent or not), counted also
 * when the provider refuses it: a gap at the receiver is frames lost.
 * @param[in] peer  A board or client id.
 * @param[in] data  [stream id or channel][payload], 1..CMD_DATA_MAX bytes.
 * @param[in] flags ROUTER_FLAG_URGENT (to the front of the provider's queue), or 0.
 * @return NULL when queued; ERR_CMD_BAD_ARG, ERR_CMD_BAD_LEN, ERR_SESSION_NO_ROUTE, or the provider's ERR_PROV_BUSY /
 *         _DOWN.
 */
MUST_USE err_h sys_session_push(uint8_t peer, cmd_in_t data, uint8_t flags);

/**
 * @brief Sends data to a provider of another board, or to a receiver of a client (SYS_SESSION.MD "Data to a provider").
 * The board it reaches sends the data on that provider as it is, without a header. NOWAIT and SPARE, never blocks; a
 * PUSH frame, counted as sys_session_push's.
 * @param[in] peer  A board or client id.
 * @param[in] to    The provider's id on `peer`.
 * @param[in] from  The sender's own id: a provider or a stream of this board.
 * @param[in] data  The data, 1..(CMD_DATA_MAX - 3) bytes.
 * @param[in] flags ROUTER_FLAG_URGENT, or 0.
 * @return NULL when queued; ERR_CMD_BAD_ARG, ERR_CMD_BAD_LEN, ERR_SESSION_NO_ROUTE, or the provider's ERR_PROV_*.
 */
MUST_USE err_h sys_session_data(uint8_t peer, uint8_t to, uint8_t from, cmd_in_t data, uint8_t flags);

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
 * its SYS_SESSION_CHANNEL(its module number)).
 * @param[in] fn The listener.
 * @return NULL when added; ERR_SESSION_FULL (every CONFIG_SYS_SESSION_PUSH_LISTENERS slot taken).
 */
MUST_USE err_h sys_session_on_push(sys_session_push_f fn);

#ifdef __cplusplus
}
#endif
