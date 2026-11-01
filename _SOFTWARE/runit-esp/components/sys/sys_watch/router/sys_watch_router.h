#pragma once

#include "router.h"
#include "sys_watch.h"

/**
 * @file sys_watch_router.h
 * @brief Watch router (root key ROUTER_KEY_WATCH = 0x0F): the remote interface of SYS_WATCH.MD "Remote". Only
 * components/app/runit includes it, to register it.
 * @usage ERR_TRY(router_register(ROUTER_KEY_WATCH, sys_watch_router));
 *
 *   code command  side   request / answer
 *   0x01 ADD      write  sys_watch_head_t, then the read; its owner: the command's src
 *   0x02 REMOVE   write  u8 id: of the command's src
 *   0x03 LIST     read   -> u8 count, then sys_watch_info_t per watch
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_WATCH_OP_ADD    0x01u
#define SYS_WATCH_OP_REMOVE 0x02u
#define SYS_WATCH_OP_LIST   0x03u

ROUTER_MODULE_DECLARE(sys_watch_router);

#ifdef __cplusplus
}
#endif
