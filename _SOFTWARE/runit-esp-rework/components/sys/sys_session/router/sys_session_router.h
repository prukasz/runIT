#pragma once

#include <stdint.h>

#include "router_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under the root key MODULE_ID_SESSION = 0x0E (SYS_SESSION.MD "Remote"), packed, little-endian;
 * contract_session.json names them. Never renumber. The router registers itself (ROUTER_MODULE_REGISTER). */

/** @brief ID value: this board's id */
typedef struct __attribute__((packed)) {
  uint8_t board_id;  /**< Kept in NVS, used from the next start */
} sys_session_id_args_t;

/** @brief ATTACH args */
typedef struct __attribute__((packed)) {
  uint8_t provider;  /**< The provider the session runs over */
  uint8_t peer;      /**< sys_session_peer_e: SLAVE, or UNKNOWN (the hello says) */
} sys_session_attach_args_t;

/** @brief DETACH args */
typedef struct __attribute__((packed)) {
  uint8_t provider;  /**< The provider whose session ends */
} sys_session_detach_args_t;

/** @brief A session as PEERS sends it: one row of its answer. */
typedef struct __attribute__((packed)) {
  uint8_t provider;     /**< The provider it runs over */
  uint8_t peer;         /**< sys_session_peer_e: what it is now (UNKNOWN before the hello of a way not told) */
  uint8_t open;         /**< 1 = both hellos passed */
  uint8_t peer_id;      /**< Its id, from its hello; SYS_SESSION_ID_NONE before it */
  uint32_t build_id;    /**< Its firmware's build, from its hello; 0 from a client */
  uint16_t timeout_ms;  /**< This board's timeout of it: the silence that ends it */
} sys_session_info_t;

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h). Sides: ID RW (the write is
 * used from the next start), ATTACH W, DETACH W, PEERS R (the value is one row: the answer is u8 count, then a row
 * per session).
 */
#define SESSION_OPERATIONS(X, P)                                             \
  X(P, OP_ID,     0x01, cmd_none_t, sys_session_id_args_t,     0)            \
  X(P, OP_ATTACH, 0x02, cmd_none_t, sys_session_attach_args_t, 0)            \
  X(P, OP_DETACH, 0x03, cmd_none_t, sys_session_detach_args_t, 0)            \
  X(P, OP_PEERS,  0x04, cmd_none_t, sys_session_info_t,        0)

CMD_TABLE_DECLARE(SESSION)

/** @brief The sessions and this board's id, under the root key MODULE_ID_SESSION. */
ROUTER_MODULE_DECLARE(sys_session_router);

#ifdef __cplusplus
}
#endif
