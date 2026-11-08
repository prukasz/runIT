#pragma once

/*
A session of the board: what sys_session.c keeps for each and the hello's admission reads. Not on the component's public
include path.
*/

#include "sdkconfig.h"
#include "sys_data_provider.h"
#include "sys_session_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a peer's hello told, kept by its session (not the wire struct sys_session_hello_t). */
typedef struct {
  uint32_t boot_id;     /**< A new one ends the session first: the peer restarted */
  uint16_t timeout_ms;  /**< The peer's timeout of this session: send every third of it */
  uint8_t id;           /**< Its own id, from its hello; SYS_SESSION_ID_NONE until then */
} sys_session_hello_state_t;

/* A session: a provider and the peer it leads to. Its fields are under the lock of sys_session.c; nothing else runs
 * under it (a send, a listener and the router are called after it is released). */
typedef struct {
  bool used;                        // false: the slot is free
  uint8_t provider;
  uint8_t peer;                     // sys_session_peer_e: what the peer is now
  uint8_t attached;                 // sys_session_peer_e given at attach: a CLIENT way a board's hello made MASTER goes
                                    // back to it when that session ends
  sys_session_hello_state_t hello;
  uint16_t silent_ms;               // time since its last frame; the session ends past its timeout (at most 60 s)
  uint16_t ping_ms;                 // time since this board's last frame on it: a ping past a third of the peer's timeout
} session_slot_t;

static inline bool session_in_use(const session_slot_t* s) {
  return s->used;
}

// Open: in use and both hellos passed, so the peer has its id (NONE before and after; a free slot's is 0).
static inline bool session_is_open(const session_slot_t* s) {
  return session_in_use(s) && s->hello.id != SYS_SESSION_ID_NONE;
}

#ifdef __cplusplus
}
#endif
