#pragma once

/*
The session's handshake: the hello (its wire struct, sending this board's, taking the peer's) and NO_SESSION. Not
on the component's public include path.
*/

#include <stdint.h>

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HELLO's body: who a board (or client) is; both ways when a session opens, again when its timeout changes.
 */
typedef struct __attribute__((packed)) {
  uint8_t version;            /**< SYS_SESSION_VERSION: the session opens only on the same one */
  uint8_t id;                 /**< Its own: a board's place, SYS_SESSION_ID_APP, or another client's pick */
  uint8_t mac[6];             /**< Its identity: the eFuse MAC; a client: 0 */
  uint32_t boot_id;           /**< Random per boot: a new one ends the session first (the peer restarted) */
  uint16_t fw_version;
  uint16_t timeout_ms;        /**< It ends the session after this much silence: send it something every third of it */
} sys_session_hello_t;

_Static_assert(sizeof(sys_session_hello_t) == 16, "hello");

/** @brief This boot's id and the board's MAC, for its hellos. Boot step of sys_session_init(). */
MUST_USE err_h sys_session_hello_init(void);

/** @brief Sends this board's hello on session `at`. */
err_h sys_session_hello_send(int32_t at);

/**
 * @brief A handshake frame from the peer of session `at`: its HELLO or its NO_SESSION (the control frames' op, then
 * their data). Other ops are not the handshake's.
 *  - HELLO: of the same version, with an id of its kind (a board 0..LAST, a client APP..CLIENT_LAST) that no other
 *    session has, and the session opens (ERR_SESSION_VERSION, ERR_SESSION_ID_TAKEN else). A new boot id ends the session
 *    first. A client is answered every hello, a board its first only (two boards that both said hello when their
 *    provider came up must not answer each other for ever); the listeners are told once.
 *  - NO_SESSION: the peer has no session with this board, so this one ends too and this board says hello.
 * @param body  The control frame's body: the op, then its data.
 * @param len   Bytes in `body`.
 */
void sys_session_hello_control(int32_t at, const uint8_t* body, uint32_t len);

/**
 * @brief The peer of session `at` sends as if it were open, and it is not here (the peer missed this board's restart:
 * UART has no disconnect; or this end timed out): ERR_SESSION_NONE is reported and the peer told NO_SESSION and this
 * board's hello, so both start over. A full queue drops them: the peer's next frame asks again.
 * @param src  The frame's source.
 */
void sys_session_hello_start_over(int32_t at, uint8_t src);

#ifdef __cplusplus
}
#endif
