#pragma once

#include "router.h"
#include "sys_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_STREAM (SYS_STREAM.MD "Remote"), each above the frame it uses (packed, little-endian;
 * contract_stream.json names them), with its sides. Never renumber; 0x05, 0x06, 0x08 and 0x0B are unused: never assign
 * them. The subscriber of SUBSCRIBE and UNSUBSCRIBE is the command's src: no frame names a peer. */

#define SYS_STREAM_OP_CREATE 0x01u  // W: sys_stream_cfg_t
/** @brief What a stream is, set at create and never changed. */
typedef struct __attribute__((packed)) sys_stream_cfg {
  uint8_t id;     /**< Its byte: 0x01..SYS_STREAM_ID_LAST */
  uint8_t dir;    /**< sys_stream_dir_e */
  uint8_t flags;  /**< SYS_STREAM_FLAG_* */
} sys_stream_cfg_t;

#define SYS_STREAM_OP_REMOVE      0x02u  // W: sys_stream_ref_t
#define SYS_STREAM_OP_UNBIND_ALL  0x0Eu  // W: sys_stream_ref_t, every binding, output and remote target
#define SYS_STREAM_OP_SUBSCRIBE   0x09u  // W: sys_stream_ref_t, the command's src gets the stream's messages
#define SYS_STREAM_OP_UNSUBSCRIBE 0x0Au  // W: sys_stream_ref_t, of the command's src
typedef struct __attribute__((packed)) {
  uint8_t id;  /**< The stream */
} sys_stream_ref_t;

#define SYS_STREAM_OP_BIND   0x03u  // W: sys_stream_binding_t
#define SYS_STREAM_OP_OUTPUT 0x0Cu  // W: sys_stream_binding_t, an IN stream's way out
#define SYS_STREAM_OP_UNBIND 0x04u  // W: sys_stream_binding_t, a binding, an output or a remote target of this provider
typedef struct __attribute__((packed)) {
  uint8_t id;                       /**< The stream */
  uint8_t provider;  /**< The provider */
} sys_stream_binding_t;

#define SYS_STREAM_OP_REMOTE 0x0Du  // W: sys_stream_remote_t
typedef struct __attribute__((packed)) {
  uint8_t id;        /**< The stream */
  uint8_t board;     /**< The board or client it goes to */
  uint8_t provider;  /**< The provider's id on that board */
} sys_stream_remote_t;

#define SYS_STREAM_OP_LIST 0x07u  // R: -> u8 count, then per stream sys_stream_head_t and head.targets sys_stream_target_t
/** @brief A stream's head: what it is, its state, how many targets follow. */
typedef struct __attribute__((packed)) {
  sys_stream_cfg_t cfg;
  uint8_t state;    /**< SYS_STREAM_STATE_* */
  uint8_t targets;  /**< How many sys_stream_target_t follow */
} sys_stream_head_t;

/** @brief A target of a stream. */
typedef struct __attribute__((packed)) {
  uint8_t type;                     /**< sys_stream_target_e */
  uint8_t peer;                     /**< PEER: its id; REMOTE: the board or client; else 0 */
  uint8_t provider;  /**< PROVIDER, OUTPUT, REMOTE: the provider; else zero */
  uint16_t dropped;                 /**< Messages not given to it: busy, down, too long, no route; stops at 0xFFFF */
} sys_stream_target_t;

/**
 * @brief The streams, their bindings and subscriptions, under ROUTER_KEY_STREAM.
 * Ops and layouts: SYS_STREAM.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_STREAM, sys_stream_router));
 */
ROUTER_MODULE_DECLARE(sys_stream_router);

#ifdef __cplusplus
}
#endif
