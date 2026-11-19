#pragma once

/**
 * @file data_matrix_router.h
 * @brief The data matrix's commands (DATA_MATRIX.MD "Commands"): the app links, unlinks and lists stream ports. Root
 * key MODULE_ID_DATA_MATRIX (0x30); router/contract_data_matrix.json names them. Packed, little-endian; never renumber.
 * The router registers itself (ROUTER_MODULE_REGISTER): no other component names it.
 */

#include "data_matrix_id.h"
#include "data_matrix.h"
#include "router_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h):
 * CONNECT     W  data_matrix_link_t  links the output of `from` to the input of `to` (data_matrix_connect)
 * DISCONNECT  W  data_matrix_link_t  removes that link (data_matrix_disconnect)
 * LIST        R  -> u8 links, data_matrix_link_t per link (data_matrix_links); ERR_CMD_OUT_FULL past the frame
 */
#define DATA_MATRIX_OPERATIONS(X, P)                                  \
  X(P, OP_CONNECT,    0x01, cmd_none_t, data_matrix_link_t, 0)        \
  X(P, OP_DISCONNECT, 0x02, cmd_none_t, data_matrix_link_t, 0)        \
  X(P, OP_LIST,       0x03, cmd_none_t, data_matrix_link_t, 0)

CMD_TABLE_DECLARE(DATA_MATRIX)

/** @brief The switch's commands, under the root key MODULE_ID_DATA_MATRIX. */
ROUTER_MODULE_DECLARE(data_matrix_router);

#ifdef __cplusplus
}
#endif
