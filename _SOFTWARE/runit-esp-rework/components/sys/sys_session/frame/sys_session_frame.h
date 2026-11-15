#pragma once

#include <stdint.h>

#include "cmd.h"
#include "sys_session_config.h"

/**
 * @file sys_session_frame.h
 * @brief A session's frames (SYS_SESSION.MD "Frames"), packed, little-endian: one 5-byte header on every frame, so a
 * frame is routed by its dst without reading or changing its body. The provider's driver takes one flat frame (`cmd_in_t`);
 * sys_session sends forwarded frames zero-copy via sys_session_send and joins separate header + body only at send.
 *
 *   request  [hdr REQUEST, flags ][key][dir][code | id + op][args]
 *   answer   [hdr ANSWER, status ][data]                 data: the answer, or the error record (ERROR)
 *   push     [hdr PUSH, flags    ][stream id][payload]   one stream's message to a subscribed peer, or a channel's (from 0x80)
 *            [hdr PUSH, flags    ][0x82 DATA][to id][from id][data]   for the provider `to id` of dst
 *   control  [hdr CONTROL        ][op][data]             HELLO: sys_session_hello_t; PEER: sys_session_peer_t
 */

/** @brief The header of every frame: CMD_HEAD bytes. */
typedef struct __attribute__((packed)) {
  uint8_t frame;  /**< Bits 0-1: ROUTER_FLAG_*; bit 2: an answer's status; bit 3: a PUSH uncounted; bits 6-7: kind */
  uint8_t dst;    /**< The board or client it goes to */
  uint8_t src;    /**< The board or client that made it; an answer goes back to it */
  uint16_t seq;   /**< Request: the source's counter; answer: echoed; push: src's counter of (dst, class); control: 0 */
} sys_session_hdr_t;

_Static_assert(sizeof(sys_session_hdr_t) == CMD_HEAD, "the header is CMD_HEAD");

/** @brief HELLO's body: who a board (or client) is; both ways when a session opens, again when its timeout changes. */
typedef struct __attribute__((packed)) {
  uint8_t id;           /**< Its own: a board's place, SYS_SESSION_ID_APP, or another client's pick */
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
