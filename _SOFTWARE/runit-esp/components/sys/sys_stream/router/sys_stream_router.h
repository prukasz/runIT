#pragma once

#include "router.h"
#include "sys_stream.h"

/**
 * @file sys_stream_router.h
 * @brief Stream router (root key ROUTER_KEY_STREAM = 0x04): the remote interface of SYS_STREAM.MD "Remote". Only
 * components/app/runit includes it, to register it. The layouts are packed (sys_stream_internal.h).
 * @usage ERR_TRY(router_register(ROUTER_KEY_STREAM, sys_stream_router));
 *
 *   code command      side   request / answer
 *   0x01 CREATE       write  sys_stream_cfg_t: u8 id, u8 dir, u8 flags
 *   0x02 REMOVE       write  u8 id
 *   0x03 BIND         write  u8 id, sys_data_provider_id_t
 *   0x04 UNBIND       write  u8 id, sys_data_provider_id_t (link 0: every binding of the stream)
 *   0x07 LIST         read   -> u8 count, then per stream its head and its targets
 *   0x09 SUBSCRIBE    write  u8 id: the command's src gets the stream's messages
 *   0x0A UNSUBSCRIBE  write  u8 id: of the command's src
 *   Codes 0x05, 0x06, 0x08 and 0x0B are unused: never assign them.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_STREAM_OP_CREATE      0x01u
#define SYS_STREAM_OP_REMOVE      0x02u
#define SYS_STREAM_OP_BIND        0x03u
#define SYS_STREAM_OP_UNBIND      0x04u
#define SYS_STREAM_OP_LIST        0x07u
#define SYS_STREAM_OP_SUBSCRIBE   0x09u
#define SYS_STREAM_OP_UNSUBSCRIBE 0x0Au

ROUTER_MODULE_DECLARE(sys_stream_router);

#ifdef __cplusplus
}
#endif
