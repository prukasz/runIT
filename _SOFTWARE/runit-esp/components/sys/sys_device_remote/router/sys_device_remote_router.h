#pragma once

#include <stdint.h>

#include "router_table.h"
#include "sys_device_remote.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under root key MODULE_ID_REMOTE = 0x2F (SYS_DEVICE_REMOTE.MD "Remote"), packed, little-endian; contract_remote.json
 * names them. The subscriber is the command's src: no frame names a peer. The router registers itself
 * (ROUTER_MODULE_REGISTER). */

/** @brief SUBSCRIBE / UNSUBSCRIBE args */
typedef struct __attribute__((packed)) {
  uint8_t id;  /**< A device of this board */
} sys_device_remote_ref_t;

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h).
 * Sides: SUBSCRIBE W, UNSUBSCRIBE W (sys_device_remote_ref_t).
 */
#define REMOTE_OPERATIONS(X, P)                                  \
  X(P, OP_SUBSCRIBE,   0x01, cmd_none_t, sys_device_remote_ref_t, 0) \
  X(P, OP_UNSUBSCRIBE, 0x02, cmd_none_t, sys_device_remote_ref_t, 0)

CMD_TABLE_DECLARE(REMOTE)

/** @brief The subscriptions to this board's devices, under MODULE_ID_REMOTE (0x2F). */
ROUTER_MODULE_DECLARE(sys_device_remote_router);

#ifdef __cplusplus
}
#endif
