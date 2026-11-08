#pragma once

#include "router.h"
#include "sys_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_SESSION (SYS_SESSION.MD "Remote"), each above the frame it uses (packed, little-endian;
 * contract_session.json names them), with its sides. Never renumber. */

#define SYS_SESSION_OP_ID 0x01u  // R: -> sys_session_id_args_t; W: sys_session_id_args_t
typedef struct __attribute__((packed)) {
  uint8_t board_id;  /**< Kept in NVS, used from the next start */
} sys_session_id_args_t;

#define SYS_SESSION_OP_ATTACH 0x02u  // W: sys_session_attach_args_t
typedef struct __attribute__((packed)) {
  uint8_t provider;
  uint8_t peer;  /**< sys_session_peer_e */
} sys_session_attach_args_t;

#define SYS_SESSION_OP_DETACH 0x03u  // W: sys_session_detach_args_t
typedef struct __attribute__((packed)) {
  uint8_t provider;
} sys_session_detach_args_t;

#define SYS_SESSION_OP_PEERS 0x04u  // R: -> u8 count, then sys_session_info_t per session
typedef struct __attribute__((packed)) {
  uint8_t provider;
  uint8_t peer;         /**< sys_session_peer_e */
  uint8_t open;         /**< 1 = both hellos passed */
  uint8_t peer_id;      /**< Its id, from its hello; SYS_SESSION_ID_NONE before it */
  uint16_t timeout_ms;  /**< This board's timeout of it: the silence that ends it */
} sys_session_info_t;

/**
 * @brief The sessions and this board's id, under ROUTER_KEY_SESSION.
 * Ops and layouts: SYS_SESSION.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_SESSION, sys_session_router));
 */
ROUTER_MODULE_DECLARE(sys_session_router);

#ifdef __cplusplus
}
#endif
