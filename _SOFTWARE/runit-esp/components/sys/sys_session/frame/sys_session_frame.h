#pragma once

#include <stdint.h>

#include "cmd.h"
#include "sys_session_config.h"

/**
 * @file sys_session_frame.h
 * @brief A session's frames (SYS_SESSION.MD "Frames"), packed, little-endian: one 5-byte header on every frame, so a
 * frame is routed by its dst without reading or changing its body. The provider's driver splits and joins a
 * frame; the session sees it whole.
 *
 *   request  [hdr REQUEST, flags ][key][dir][code | id u16 + op u16][args]
 *   answer   [hdr ANSWER, status ][data]                 data: the answer, or the error record (ERROR)
 *   stream   [hdr STREAM, flags  ][connector][payload]   one connector frame, to a peer subscribed to it
 *   control  [hdr CONTROL        ][op][data]             HELLO: sys_session_hello_t; GONE: [count u8][ids]
 */

/** @brief The header of every frame: CMD_HEAD bytes. */
typedef struct __attribute__((packed)) {
  uint8_t frame;  /**< Bits 0-1: ROUTER_FLAG_*; bit 2: an answer's status; bits 6-7: the kind (SYS_SESSION_FRAME_*) */
  uint8_t dst;    /**< The board or client it goes to */
  uint8_t src;    /**< The board or client that made it; an answer goes back to it */
  uint16_t seq;   /**< Request: the source's counter; answer: echoed; stream: its stream's counter; control: 0 */
} sys_session_hdr_t;

/**
 * @brief HELLO's body: who a board (or client) is; both ways when a session opens, again when its subtree or its
 * timeout changes.
 */
typedef struct __attribute__((packed)) {
  uint8_t version;            /**< SYS_SESSION_VERSION: the session opens only on the same one */
  uint8_t board_id;           /**< Its place; a client: SYS_SESSION_ID_NONE */
  uint8_t your_id;            /**< The root's answer to a client: the id it gave it; else SYS_SESSION_ID_NONE */
  uint8_t mac[6];             /**< Its identity: the eFuse MAC; a client: 0 */
  uint32_t boot_id;           /**< Random per boot: a new one ends the session first (the peer restarted) */
  uint32_t descriptors_hash;  /**< Of the descriptors it was built with (contracts, devices, errors) */
  uint16_t board_type;        /**< board.json */
  uint16_t fw_version;
  uint16_t timeout_ms;        /**< It ends the session after this much silence: send it something every third of it */
  uint8_t child_count;
  uint8_t children[];         /**< To its parent: the boards reached through it (its subtree); else none */
} sys_session_hello_t;

_Static_assert(sizeof(sys_session_hdr_t) == CMD_HEAD, "the header is CMD_HEAD");
_Static_assert(sizeof(sys_session_hello_t) == 24, "hello without children");
