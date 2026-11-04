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
 *   stream   [hdr STREAM, flags  ][stream id][payload]   one stream's frame to a subscribed peer, or a channel's (id from 0x80)
 *   control  [hdr CONTROL        ][op][data]             HELLO: sys_session_hello_t (hello/sys_session_hello.h);
 *                                                        GONE: [client u8], dst 0xFF
 */

/** @brief The header of every frame: CMD_HEAD bytes. */
typedef struct __attribute__((packed)) {
  uint8_t frame;  /**< Bits 0-1: ROUTER_FLAG_*; bit 2: an answer's status; bits 6-7: the kind (SYS_SESSION_FRAME_*) */
  uint8_t dst;    /**< The board or client it goes to */
  uint8_t src;    /**< The board or client that made it; an answer goes back to it */
  uint16_t seq;   /**< Request: the source's counter; answer: echoed; stream: its stream's counter; control: 0 */
} sys_session_hdr_t;

_Static_assert(sizeof(sys_session_hdr_t) == CMD_HEAD, "the header is CMD_HEAD");
