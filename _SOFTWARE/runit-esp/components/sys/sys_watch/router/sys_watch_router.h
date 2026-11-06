#pragma once

#include "router.h"
#include "sys_watch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_WATCH (SYS_WATCH.MD "Remote"), each above the frame it uses (packed, little-endian;
 * contract_watch.json names them), with its sides. Never renumber. The owner of a watch is the command's src: no frame
 * names a peer. */

#define SYS_WATCH_OP_ADD 0x01u  // W: sys_watch_head_t + the read command (cmd_in_t tail), its owner: the command's src

#define SYS_WATCH_OP_REMOVE 0x02u  // W: sys_watch_ref_t, of the command's src
typedef struct __attribute__((packed)) {
  uint8_t id;  /**< The watch */
} sys_watch_ref_t;

#define SYS_WATCH_OP_LIST 0x03u  // R: -> u8 count, then sys_watch_info_t per watch

/**
 * @brief The watches, under ROUTER_KEY_WATCH (0x0F). Only components/app/runit includes it, to register it.
 * Ops and layouts: SYS_WATCH.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_WATCH, sys_watch_router));
 */
ROUTER_MODULE_DECLARE(sys_watch_router);

#ifdef __cplusplus
}
#endif
