#pragma once

#include <stdint.h>

#include "router_table.h"
#include "sys_ble.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under the root key MODULE_ID_BLE = 0x2D (SYS_BLE.MD "Remote"), packed, little-endian; contract_ble.json names
 * them. Never renumber. The flags are in sys_ble.h. The router registers itself (ROUTER_MODULE_REGISTER). */

/** @brief STATUS answer */
typedef struct __attribute__((packed)) {
  uint8_t connected;  /**< 1 = a client is connected */
  uint16_t mtu;       /**< The link's MTU */
} sys_ble_status_t;

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h). Sides: STATUS R, LIST R
 * (answer: u8 count, then per service {u16 uuid, u8 flags, u8 n, n x {u16 uuid, u8 flags, u16 tx, name NUL-ended}}).
 * The GATT table is changed by the providers (provider_ble), never here.
 */
#define BLE_OPERATIONS(X, P)                                     \
  X(P, OP_STATUS, 0x01, cmd_none_t, sys_ble_status_t, 0)         \
  X(P, OP_LIST,   0x02, cmd_none_t, cmd_none_t,       0)

CMD_TABLE_DECLARE(BLE)

/** @brief The GATT table (read) and the link's state, under the root key MODULE_ID_BLE. */
ROUTER_MODULE_DECLARE(sys_ble_router);

#ifdef __cplusplus
}
#endif
