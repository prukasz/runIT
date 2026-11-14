#pragma once

#include <stdint.h>

#include "router_table.h"
#include "sys_watch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under root key MODULE_ID_WATCH = 0x2B (SYS_WATCH.MD "Remote"), packed, little-endian;
 * contract_watch.json names them. The router registers itself (ROUTER_MODULE_REGISTER). */

/** @brief REMOVE args */
typedef struct __attribute__((packed)) {
  uint8_t id;  /**< The watch id to remove */
} sys_watch_ref_t;

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h).
 * Sides: ADD W (sys_watch_head_t + read command tail), REMOVE W (sys_watch_ref_t),
 *        LIST R (answer: u8 count + count * sys_watch_info_t).
 */
#define WATCH_OPERATIONS(X, P)                           \
  X(P, OP_ADD,    0x01, cmd_none_t, sys_watch_head_t, 0) \
  X(P, OP_REMOVE, 0x02, cmd_none_t, sys_watch_ref_t,  0) \
  X(P, OP_LIST,   0x03, cmd_none_t, sys_watch_info_t, 0)

CMD_TABLE_DECLARE(WATCH)

/** @brief Watch router under MODULE_ID_WATCH (0x2B). */
ROUTER_MODULE_DECLARE(sys_watch_router);

#ifdef __cplusplus
}
#endif
