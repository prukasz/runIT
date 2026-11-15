#pragma once

/*
The session's own files and nothing else: its router (PEERS), the hello (hello/), the calls (call/) and the board's
identity (board/). Not on the component's public include path. Each function takes the sessions' lock itself.
*/

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "sys_data_provider.h"
#include "sys_session.h"
#include "sys_session_frame.h"
#include "sys_session_router.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HDR               ((uint32_t)sizeof(sys_session_hdr_t))                 // the header in front of every frame
#define FRAME(kind)       ((uint8_t)((kind) << 6))                              // the frame byte of a kind
#define FRAME_KIND(frame) ((uint8_t)(((frame) & SYS_SESSION_FRAME_KIND) >> 6))  // the kind of a frame byte

/**
 * @brief The provider's send flags for a frame byte: NOWAIT always, URGENT when the frame says so, SPARE for a PUSH
 * (a frame that may be dropped never takes the room an answer needs).
 */
static inline uint8_t sys_session_send_flags(uint8_t frame) {
  return (uint8_t)(CMD_SEND_NOWAIT | (frame & CMD_SEND_URGENT) |
                   (FRAME_KIND(frame) == SYS_SESSION_PUSH ? CMD_SEND_SPARE : 0u));
}

/* ----------------------------------------------------------------------------
 * A session
 * ------------------------------------------------------------------------- */

/** @brief What a peer's hello told, kept by its session (not the wire struct sys_session_hello_t). */
typedef struct {
  uint32_t boot_id;     /**< A new one ends the session first: the peer restarted */
  uint32_t build_id;    /**< Its firmware's build (PEERS shows it) */
  uint16_t timeout_ms;  /**< The peer's timeout of this session: send every third of it */
  uint8_t id;           /**< Its own id, from its hello; SYS_SESSION_ID_NONE until then and after it ends */
} sys_session_hello_state_t;

/* A session: a provider and the way it leads. Its fields are under the lock of sys_session.c; nothing else runs under
 * it (a send, a listener and the router are called after it is released). The peer's role is the way's and the
 * hello's (sys_session_role): no field of its own. */
typedef struct {
  bool used;           // false: the slot is free
  bool none_told;      // ERR_SESSION_NONE reported since the session closed: once per closing
  uint8_t provider;
  uint8_t way;         // sys_session_way_e, given at attach
  sys_session_hello_state_t hello;
  uint16_t silent_ms;  // time since its last frame; the session ends past its timeout (at most 60 s)
  uint16_t ping_ms;    // time since this board's last frame on it: a ping past a third of the peer's timeout
} session_slot_t;

// Open: in use and both hellos passed, so the peer has its id.
static inline bool session_is_open(const session_slot_t* s) {
  return s->used && s->hello.id != SYS_SESSION_ID_NONE;
}

/** @brief The role of a session's peer: DOWN is a slave; UP is the master for a board's id, a client for a client's. */
static inline uint8_t sys_session_role(const session_slot_t* s) {
  if (s->way == SYS_SESSION_DOWN) {
    return SYS_SESSION_SLAVE;
  }
  if (s->hello.id == SYS_SESSION_ID_NONE) {
    return SYS_SESSION_UNKNOWN;
  }
  return s->hello.id <= SYS_SESSION_ID_LAST ? SYS_SESSION_MASTER : SYS_SESSION_CLIENT;
}

/* ----------------------------------------------------------------------------
 * Sessions
 * ------------------------------------------------------------------------- */

/** @brief The session a frame for `dst` leaves on, -1: no way (a slave: its master's; the root: that peer's). */
int32_t sys_session_route(uint8_t dst);

/** @brief A copy of session `at`. */
session_slot_t sys_session_slot(int32_t at);

/** @brief This board's timeout of session `at`: what its hello tells the peer. */
uint32_t sys_session_timeout(int32_t at);

/** @brief Whether session `at` may open: the board has no master, or it is that master's session. */
bool sys_session_admits(int32_t at);

/** @brief Ends every open session but `at`: a board that takes a master has no other session. */
void sys_session_end_others(int32_t at);

/**
 * @brief Opens session `at` with what its peer's hello said, unless another open session has its id. When it was not
 * open, the reached listeners are told and the root tells its other peers (PEER up).
 * @param was_open  Out: whether it was open already.
 * @return true when open.
 */
bool sys_session_open(int32_t at, const sys_session_hello_state_t* peer, bool* was_open);

/** @brief Ends session `at`: its calls fail, its peer is gone, the root tells its other peers (PEER down). */
void sys_session_end(int32_t at);

/**
 * @brief The peer of session `at` sends as if it were open, and it is not here (it missed this board's restart: UART
 * has no disconnect; or this end timed out): it is told NO_SESSION and this board's hello, so both start over.
 * ERR_SESSION_NONE is reported once per closing of the session, not per frame.
 */
void sys_session_start_over(int32_t at, uint8_t src);

/* ----------------------------------------------------------------------------
 * Frames out
 * ------------------------------------------------------------------------- */

/** @brief One flat frame on session `at`'s provider. */
MUST_USE err_h sys_session_send(int32_t at, cmd_in_t frame, uint8_t flags);
MUST_USE err_h sys_session_send_parts(int32_t at, const void* head, uint32_t head_len, const void* body,
                                      uint32_t body_len, uint8_t flags);

/** @brief A control frame on session `at`: `op`, then `data`. */
MUST_USE err_h sys_session_send_control(int32_t at, uint8_t op, const void* data, uint32_t len);

/** @brief A broadcast request (the header, the command): a slave's to its master, the root's to every slave. */
MUST_USE err_h sys_session_send_broadcast(const sys_session_hdr_t* hdr, cmd_in_t req, uint8_t flags);

/** @brief Copies every session out as PEERS sends it; returns how many. */
uint8_t sys_session_list(sys_session_info_t out[CONFIG_SYS_SESSION_MAX]);

/* ----------------------------------------------------------------------------
 * The hello (hello/)
 * ------------------------------------------------------------------------- */

/** @brief Sends this board's hello on session `at`. */
MUST_USE err_h sys_session_hello_send(int32_t at);

/**
 * @brief A handshake frame from the peer of session `at`: its HELLO or its NO_SESSION (the op, then its data).
 *  - HELLO: an id of its kind (a board 0..LAST but this board's, a client APP..CLIENT_LAST) that no other session has
 *    (ERR_SESSION_ID_TAKEN else), and the session opens. A new boot id ends the session first. A board's hello on an
 *    UP way makes it this board's master: the other sessions end, and none opens while it lasts. A client is answered
 *    every hello, a board its first only (two boards that both said hello must not answer each other for ever).
 *  - NO_SESSION: the peer has no session with this board, so this one ends too and this board says hello.
 */
void sys_session_hello_control(int32_t at, const uint8_t* body, uint32_t len);

/* ----------------------------------------------------------------------------
 * The calls (call/)
 * ------------------------------------------------------------------------- */

/** @brief An answer frame for this board: it ends the call of its seq to its src (a late one is dropped). */
void sys_session_call_answer(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len);

/** @brief Session `at` ended: the calls that went through it end with ERR_SESSION_PEER_DOWN. */
void sys_session_call_fail_via(int32_t at);

/** @brief The calls whose time is up end with ERR_SESSION_TIMEOUT. The session's tick. */
void sys_session_call_expire(uint32_t now_ms);

/* ----------------------------------------------------------------------------
 * The board's identity (board/: NVS, the random boot id, the firmware's build; on the host, the tests')
 * ------------------------------------------------------------------------- */

/** @brief The board id kept in NVS; CONFIG_SYS_SESSION_BOARD_ID_DEFAULT when none is. */
uint8_t sys_session_board_load_id(void);

/** @brief Keeps `id` in NVS. */
MUST_USE err_h sys_session_board_save_id(uint8_t id);

/** @brief A random id for this boot. */
uint32_t sys_session_board_boot_id(void);

/** @brief The running firmware's build: the first 4 bytes of its ELF's SHA-256. */
uint32_t sys_session_board_build_id(void);

#ifdef __cplusplus
}
#endif
