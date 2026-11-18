#pragma once

#include <stdint.h>

#include "sys_board_id.h"
#include "router_table.h"
#include "sys_nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under the root key MODULE_ID_BOARD = 0x2D (SYS_BOARD.MD "Remote"), packed, little-endian; contract_board.json
 * names them. Never renumber. The router registers itself (ROUTER_MODULE_REGISTER). */

/** @brief JSON_APPEND args: the data follows */
typedef struct __attribute__((packed)) {
  uint32_t offset;  /**< The file's length */
} sys_board_append_args_t;

/** @brief JSON_READ args */
typedef struct __attribute__((packed)) {
  uint16_t chunk;  /**< 0..chunks-1 */
} sys_board_read_args_t;

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h). Sides: JSON_APPEND W (the value
 * is the head, the data follows), JSON_ERASE W, JSON_INFO R, JSON_READ R (answer: the chunk's bytes).
 */
#define BOARD_OPERATIONS(X, P)                                                   \
  X(P, OP_JSON_APPEND, 0x01, cmd_none_t,            sys_board_append_args_t, 0)  \
  X(P, OP_JSON_ERASE,  0x02, cmd_none_t,            cmd_none_t,              0)  \
  X(P, OP_JSON_INFO,   0x03, cmd_none_t,            sys_nvs_json_info_t,     0)  \
  X(P, OP_JSON_READ,   0x04, sys_board_read_args_t, cmd_none_t,              0)

CMD_TABLE_DECLARE(BOARD)

/** @brief The board's JSON descriptor file, under the root key MODULE_ID_BOARD. */
ROUTER_MODULE_DECLARE(sys_board_router);

#ifdef __cplusplus
}
#endif
