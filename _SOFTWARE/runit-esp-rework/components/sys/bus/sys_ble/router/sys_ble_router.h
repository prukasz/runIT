#pragma once

#include <stdint.h>

#include "router_table.h"
#include "sys_ble.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under the root key MODULE_ID_BLE = 0x2E (SYS_BLE.MD "Remote"), packed, little-endian; contract_ble.json names
 * them. Never renumber. Changes are staged; APPLY puts them live. The router registers itself
 * (ROUTER_MODULE_REGISTER). */

/* Flags of a service (SVC_CREATE, LIST) */
#define SYS_BLE_SVC_PRIMARY (1u << 0)  /**< A primary service */
#define SYS_BLE_SVC_LOCKED  (1u << 1)  /**< LIST only: the board's own, no change, no removal */

/* Flags of a characteristic (CHAR_CREATE, LIST) */
#define SYS_BLE_CHR_WRITE    (1u << 0)  /**< The peer writes it */
#define SYS_BLE_CHR_INDICATE (1u << 1)  /**< Out as indications */
#define SYS_BLE_CHR_NOTIFY   (1u << 2)  /**< Out as notifications */
#define SYS_BLE_CHR_FRAMED   (1u << 3)  /**< Fragment header on every packet (SYS_BLE_FRAG_*) */

/** @brief SVC_CREATE args */
typedef struct __attribute__((packed)) {
  uint16_t uuid;  /**< The service */
  uint8_t flags;  /**< SYS_BLE_SVC_PRIMARY */
} sys_ble_svc_args_t;

/** @brief SVC_REMOVE / CHAR_REMOVE args */
typedef struct __attribute__((packed)) {
  uint16_t uuid;  /**< The service / the characteristic (unique in the table) */
} sys_ble_uuid_args_t;

/** @brief CHAR_CREATE args: its name follows (UTF-8, NUL-ended; none: nothing), the descriptor 0x2901 */
typedef struct __attribute__((packed)) {
  uint16_t svc;   /**< Its service */
  uint16_t uuid;  /**< The characteristic */
  uint8_t flags;  /**< SYS_BLE_CHR_* */
  uint16_t tx;    /**< Its TX buffer in bytes, 0 = none (needs NOTIFY or INDICATE) */
} sys_ble_char_args_t;

/** @brief STATUS answer */
typedef struct __attribute__((packed)) {
  uint8_t connected;  /**< 1 = a client is connected */
  uint16_t mtu;       /**< The link's MTU */
} sys_ble_status_t;

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h). Sides: SVC_CREATE W,
 * SVC_REMOVE W, CHAR_CREATE W (the value is the head, the name follows), CHAR_REMOVE W, APPLY W, STATUS R, LIST R
 * (answer: u8 count, then per service {u16 uuid, u8 flags, u8 n, n x {u16 uuid, u8 flags, u16 tx, name NUL-ended}},
 * the table as the next APPLY leaves it).
 */
#define BLE_OPERATIONS(X, P)                                                \
  X(P, OP_SVC_CREATE,  0x01, cmd_none_t, sys_ble_svc_args_t,  0)            \
  X(P, OP_SVC_REMOVE,  0x02, cmd_none_t, sys_ble_uuid_args_t, 0)            \
  X(P, OP_CHAR_CREATE, 0x03, cmd_none_t, sys_ble_char_args_t, 0)            \
  X(P, OP_CHAR_REMOVE, 0x04, cmd_none_t, sys_ble_uuid_args_t, 0)            \
  X(P, OP_APPLY,       0x05, cmd_none_t, cmd_none_t,          0)            \
  X(P, OP_STATUS,      0x06, cmd_none_t, sys_ble_status_t,    0)            \
  X(P, OP_LIST,        0x07, cmd_none_t, cmd_none_t,          0)

CMD_TABLE_DECLARE(BLE)

/** @brief The run-time GATT table and the link's state, under the root key MODULE_ID_BLE. */
ROUTER_MODULE_DECLARE(sys_ble_router);

#ifdef __cplusplus
}
#endif
