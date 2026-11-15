#pragma once

#include "router_table.h"
#include "sys_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under root key MODULE_ID_STREAM = 0x29 (SYS_STREAM.MD "Remote"), packed, little-endian; contract_stream.json
 * names them. The router registers itself (ROUTER_MODULE_REGISTER): no other component names it. The subscriber of
 * SUBSCRIBE and UNSUBSCRIBE is the command's src: no frame names a peer. */

/** @brief What a stream is, set at create and never changed. */
typedef struct __attribute__((packed)) sys_stream_cfg {
  uint8_t id;     /**< Its byte: 0x01..SYS_STREAM_ID_LAST */
  uint8_t flags;  /**< SYS_STREAM_FLAG_* */
} sys_stream_cfg_t;

/** @brief REMOVE / SUBSCRIBE / UNSUBSCRIBE args */
typedef struct __attribute__((packed)) {
  uint8_t id;  /**< The stream */
} sys_stream_ref_t;

/** @brief SOURCE / UNBIND args */
typedef struct __attribute__((packed)) {
  uint8_t id;        /**< The stream */
  uint8_t provider;  /**< The provider; UNBIND: SYS_DATA_PROVIDER_ID_NONE (0xFF) for every one */
} sys_stream_source_t;

/** @brief SINK args */
typedef struct __attribute__((packed)) {
  uint8_t id;        /**< The stream */
  uint8_t board;     /**< Target board or client id (this board's id or SYS_SESSION_ID_SELF for a local provider) */
  uint8_t provider;  /**< The provider's id on that board */
} sys_stream_sink_t;

/** @brief LIST answer: a stream's head, what it is and how many targets follow (a system stream: its id). */
typedef struct __attribute__((packed)) {
  sys_stream_cfg_t cfg;  /**< What the stream is */
  uint8_t targets;       /**< How many sys_stream_target_t follow */
} sys_stream_head_t;

/** @brief LIST answer: a target of a stream. */
typedef struct __attribute__((packed)) {
  uint8_t type;      /**< sys_stream_target_e */
  uint8_t peer;      /**< PEER: its id; SINK: the board or client (SYS_SESSION_ID_SELF on this board); else 0 */
  uint8_t provider;  /**< SOURCE / SINK: the provider; else 0 */
} sys_stream_target_t;

_Static_assert(sizeof(sys_stream_head_t) == 3 && sizeof(sys_stream_target_t) == 3, "LIST rows");

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h).
 * Sides: CREATE W, REMOVE W, SOURCE W, SINK W, UNBIND W, LIST R (answer: u8 count, then per stream
 * sys_stream_head_t and head.targets sys_stream_target_t), SUBSCRIBE W, UNSUBSCRIBE W.
 */
#define STREAM_OPERATIONS(X, P)                                            \
  X(P, OP_CREATE,      0x01, cmd_none_t, sys_stream_cfg_t,    0)           \
  X(P, OP_REMOVE,      0x02, cmd_none_t, sys_stream_ref_t,    0)           \
  X(P, OP_SOURCE,      0x03, cmd_none_t, sys_stream_source_t, 0)           \
  X(P, OP_SINK,        0x04, cmd_none_t, sys_stream_sink_t,   0)           \
  X(P, OP_UNBIND,      0x05, cmd_none_t, sys_stream_source_t, 0)           \
  X(P, OP_LIST,        0x07, cmd_none_t, sys_stream_head_t,   0)           \
  X(P, OP_SUBSCRIBE,   0x08, cmd_none_t, sys_stream_ref_t,    0)           \
  X(P, OP_UNSUBSCRIBE, 0x09, cmd_none_t, sys_stream_ref_t,    0)

CMD_TABLE_DECLARE(STREAM)

/** @brief The streams, their sources, sinks and subscriptions, under root key MODULE_ID_STREAM. */
ROUTER_MODULE_DECLARE(sys_stream_router);

#ifdef __cplusplus
}
#endif
