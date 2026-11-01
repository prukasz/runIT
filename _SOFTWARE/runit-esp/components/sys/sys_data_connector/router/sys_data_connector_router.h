#pragma once

#include "router.h"
#include "sys_data_connector.h"

/**
 * @file sys_data_connector_router.h
 * @brief Connector router (root key ROUTER_KEY_CONN = 0x04): the remote interface of SYS_DATA_CONNECTOR.MD "Remote".
 * Only components/app/runit includes it, to register it. The layouts are packed (sys_data_connector_internal.h).
 * @usage ERR_TRY(router_register(ROUTER_KEY_CONN, sys_data_connector_router));
 *
 *   code command      side   request / answer
 *   0x01 CREATE       write  sys_data_connector_cfg_t: u8 id, u8 rx, u8 flags, u16 rx_size
 *   0x02 REMOVE       write  u8 id
 *   0x03 BIND         write  u8 id, sys_data_provider_id_t, u8 flags (SYS_DATA_CONNECTOR_BIND_RX / _TX)
 *   0x04 UNBIND       write  u8 id, sys_data_provider_id_t (link 0: every binding of the connector)
 *   0x05 SUSPEND      write  u8 id
 *   0x06 RESUME       write  u8 id
 *   0x07 LIST         read   -> u8 count, then per connector its head, its subscribers, its bindings
 *   0x08 READ         read   u8 id -> sys_data_connector_msg_t, then the bytes
 *   0x09 SUBSCRIBE    write  u8 id: the command's src gets the connector's frames
 *   0x0A UNSUBSCRIBE  write  u8 id: of the command's src
 *   0x0B MIRROR       write  u8 id, u8 board, u8 remote_id: the mirror of that board's connector (0: ends it)
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_DATA_CONNECTOR_OP_CREATE      0x01u
#define SYS_DATA_CONNECTOR_OP_REMOVE      0x02u
#define SYS_DATA_CONNECTOR_OP_BIND        0x03u
#define SYS_DATA_CONNECTOR_OP_UNBIND      0x04u
#define SYS_DATA_CONNECTOR_OP_SUSPEND     0x05u
#define SYS_DATA_CONNECTOR_OP_RESUME      0x06u
#define SYS_DATA_CONNECTOR_OP_LIST        0x07u
#define SYS_DATA_CONNECTOR_OP_READ        0x08u
#define SYS_DATA_CONNECTOR_OP_SUBSCRIBE   0x09u
#define SYS_DATA_CONNECTOR_OP_UNSUBSCRIBE 0x0Au
#define SYS_DATA_CONNECTOR_OP_MIRROR      0x0Bu

ROUTER_MODULE_DECLARE(sys_data_connector_router);

#ifdef __cplusplus
}
#endif
