#pragma once

#include <stdint.h>

#include "data_matrix.h"
#include "router_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under the root key MODULE_ID_DATA_MATRIX = 0x30 (DATA_MATRIX.MD "Commands"), packed, little-endian;
 * contract_data_matrix.json names them. Never renumber. The router registers itself (ROUTER_MODULE_REGISTER): no other
 * component names it. */

/** @brief CLEAR_ROLE args */
typedef struct __attribute__((packed)) {
  uint8_t provider;  /**< The provider whose role is cleared */
} data_matrix_provider_args_t;

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h).
 * Sides: ROLE W, CLEAR_ROLE W, ROUTE W, UNROUTE W, CLEAR W, LIST R (answer: u8 roles, data_matrix_role_args_t per role,
 * u8 routes, data_matrix_route_t per route).
 */
#define DATA_MATRIX_OPERATIONS(X, P)                                          \
  X(P, OP_ROLE,       0x01, cmd_none_t, data_matrix_role_args_t,     0)       \
  X(P, OP_CLEAR_ROLE, 0x02, cmd_none_t, data_matrix_provider_args_t, 0)       \
  X(P, OP_ROUTE,      0x03, cmd_none_t, data_matrix_route_t,         0)       \
  X(P, OP_UNROUTE,    0x04, cmd_none_t, data_matrix_route_t,         0)       \
  X(P, OP_CLEAR,      0x05, cmd_none_t, data_matrix_end_t,           0)       \
  X(P, OP_LIST,       0x06, cmd_none_t, data_matrix_route_t,         0)

CMD_TABLE_DECLARE(DATA_MATRIX)

/** @brief The roles and the routes, under the root key MODULE_ID_DATA_MATRIX. */
ROUTER_MODULE_DECLARE(data_matrix_router);

#ifdef __cplusplus
}
#endif
