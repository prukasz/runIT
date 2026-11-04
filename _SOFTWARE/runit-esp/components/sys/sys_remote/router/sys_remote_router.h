#pragma once

#include "router.h"
#include "sys_remote.h"

/**
 * @file sys_remote_router.h
 * @brief Remote router (root key ROUTER_KEY_REMOTE = 0x10): the remote interface of SYS_REMOTE.MD "Subscriptions".
 * Only components/app/runit includes it, to register it.
 * @usage ERR_TRY(router_register(ROUTER_KEY_REMOTE, sys_remote_router));
 *
 *   code command      side   request / answer
 *   0x01 SUBSCRIBE    write  u16 id: a device of this board; the subscriber is the command's src
 *   0x02 UNSUBSCRIBE  write  u16 id: of the command's src
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_REMOTE_OP_SUBSCRIBE   0x01u
#define SYS_REMOTE_OP_UNSUBSCRIBE 0x02u

ROUTER_MODULE_DECLARE(sys_remote_router);

#ifdef __cplusplus
}
#endif
