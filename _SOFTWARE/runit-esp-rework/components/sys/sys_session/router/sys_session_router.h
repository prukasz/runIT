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

/** @brief A session as PEERS sends it: one row of its answer. */
typedef struct __attribute__((packed)) {
  uint8_t provider;     /**< The provider it runs over */
  uint8_t open;         /**< 1 = both hellos passed */
  uint8_t peer_id;      /**< Its id, from its hello; SYS_SESSION_ID_NONE before it */
  uint8_t safety;       /**< 1: its end is a safety event */
  uint32_t build_id;    /**< Its firmware's build, from its hello; 0 from a client */
  uint16_t deadman_ms;  /**< This board's dead-man's time of it: the silence that ends it; 0: none */
} sys_session_info_t;

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h). Sides: ID RW (the write is
 * used from the next start), PEERS R (the value is one row: the answer is u8 count, then a row per session). A
 * provider becomes a session by its role (data_matrix ROLE).
 */
#define SESSION_OPERATIONS(X, P)                                             \
  X(P, OP_ID,     0x01, cmd_none_t, sys_session_id_args_t,     0)            \
  X(P, OP_PEERS,  0x02, cmd_none_t, sys_session_info_t,        0)

CMD_TABLE_DECLARE(SESSION)

/** @brief The sessions and this board's id, under the root key MODULE_ID_SESSION. */
ROUTER_MODULE_DECLARE(sys_session_router);

#ifdef __cplusplus
}
#endif
