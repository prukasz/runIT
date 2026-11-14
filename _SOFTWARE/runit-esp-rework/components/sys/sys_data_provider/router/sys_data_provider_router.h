#pragma once

#include "router_table.h"
#include "sys_data_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under the root key MODULE_ID_PROVIDER = 0x0D (SYS_DATA_PROVIDER.MD "Remote"), packed, little-endian;
 * contract_provider.json names them. Never renumber. A provider is created by its driver's own command, from its own
 * config (a BLE characteristic, a UART port): no op here creates one. The router registers itself
 * (ROUTER_MODULE_REGISTER): no component names it. */

/** @brief REMOVE / BIND_COMMANDS / UNBIND_COMMANDS args, SEND's head */
typedef struct __attribute__((packed)) {
  uint8_t provider;  /**< Its id */
} sys_data_provider_ref_t;

/** @brief A provider as LIST sends it: one row of its answer. */
typedef struct __attribute__((packed)) {
  uint8_t provider;  /**< Its id */
  uint8_t link;      /**< sys_data_provider_link_e */
  uint8_t state;     /**< SYS_DATA_PROVIDER_STATE_*: the provider's own, as is */
} sys_data_provider_info_t;

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h). Sides: REMOVE W, LIST R (the
 * value is one row: the answer is u8 count, then a row per provider), SEND W (the head, then the frame as its tail),
 * BIND_COMMANDS W (its messages are commands for this board, no header), UNBIND_COMMANDS W.
 */
#define PROVIDER_OPERATIONS(X, P)                                                \
  X(P, OP_REMOVE,          0x01, cmd_none_t, sys_data_provider_ref_t,  0)         \
  X(P, OP_LIST,            0x02, cmd_none_t, sys_data_provider_info_t, 0)         \
  X(P, OP_SEND,            0x03, cmd_none_t, sys_data_provider_ref_t,  0)         \
  X(P, OP_BIND_COMMANDS,   0x04, cmd_none_t, sys_data_provider_ref_t,  0)         \
  X(P, OP_UNBIND_COMMANDS, 0x05, cmd_none_t, sys_data_provider_ref_t,  0)

CMD_TABLE_DECLARE(PROVIDER)

/** @brief The data providers, under the root key MODULE_ID_PROVIDER. */
ROUTER_MODULE_DECLARE(sys_data_provider_router);

#ifdef __cplusplus
}
#endif
