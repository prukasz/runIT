#pragma once

#include "router.h"
#include "sys_remote.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_REMOTE (SYS_REMOTE.MD "Remote"), each above the frame it uses (packed, little-endian;
 * contract_remote.json names them), with its sides. Never renumber. The subscriber is the command's src: no frame
 * names a peer. */

#define SYS_REMOTE_OP_SUBSCRIBE   0x01u  // W: sys_remote_device_ref_t
#define SYS_REMOTE_OP_UNSUBSCRIBE 0x02u  // W: sys_remote_device_ref_t
typedef struct __attribute__((packed)) {
  uint16_t id;  /**< A device of this board */
} sys_remote_device_ref_t;

/**
 * @brief The subscriptions to this board's devices, under ROUTER_KEY_REMOTE.
 * Ops and layouts: SYS_REMOTE.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_REMOTE, sys_remote_router));
 */
ROUTER_MODULE_DECLARE(sys_remote_router);

#ifdef __cplusplus
}
#endif
