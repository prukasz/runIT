#pragma once

#include "router.h"
#include "sys_session.h"

/**
 * @file sys_session_router.h
 * @brief Session router (root key ROUTER_KEY_SESSION = 0x0E): the remote interface of SYS_SESSION.MD "Remote". Only
 * components/app/runit includes it, to register it.
 * @usage ERR_TRY(router_register(ROUTER_KEY_SESSION, sys_session_router));
 *
 *   code command  side   request / answer
 *   0x01 ID       rw     write: u8 board_id (kept in NVS, used from the next start); read: -> u8 board_id
 *   0x02 ATTACH   write  sys_data_provider_id_t, u8 peer, u8 access
 *   0x03 DETACH   write  sys_data_provider_id_t
 *   0x04 PEERS    read   -> u8 count, then sys_session_info_t per session
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_SESSION_OP_ID     0x01u
#define SYS_SESSION_OP_ATTACH 0x02u
#define SYS_SESSION_OP_DETACH 0x03u
#define SYS_SESSION_OP_PEERS  0x04u

/** @brief A session as PEERS sends it. */
typedef struct __attribute__((packed)) {
  sys_data_provider_id_t provider;
  uint8_t peer;         /**< sys_session_peer_e */
  uint8_t access;       /**< sys_session_access_e */
  uint8_t open;         /**< 1 = both hellos passed */
  uint8_t peer_id;      /**< Its board id, or the client id it was given; SYS_SESSION_ID_NONE before the hello */
  uint16_t timeout_ms;  /**< This board's timeout of it: the silence that ends it */
  uint8_t subtree;      /**< Boards reached through it */
} sys_session_info_t;

ROUTER_MODULE_DECLARE(sys_session_router);

#ifdef __cplusplus
}
#endif
