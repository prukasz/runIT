#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_OTA (SYS_OTA.MD "Remote"), each above the frame it uses (packed, little-endian; contract_ota.json
 * names them), with its sides. Never renumber. */

#define SYS_OTA_OP_BEGIN 0x01u  // W: sys_ota_begin_args_t
typedef struct __attribute__((packed)) {
  uint32_t size;  /**< The image's length in bytes */
} sys_ota_begin_args_t;

#define SYS_OTA_OP_WRITE 0x02u  // W: sys_ota_write_args_t + data
typedef struct __attribute__((packed)) {
  uint32_t offset;  /**< The bytes so far; the data follows */
} sys_ota_write_args_t;

#define SYS_OTA_OP_COMMIT  0x03u  // W: -, the next boot starts the image; the app restarts the board (SYS RESTART)
#define SYS_OTA_OP_ABORT   0x04u  // W: -
#define SYS_OTA_OP_CONFIRM 0x06u  // W: -, keeps the running firmware

#define SYS_OTA_OP_INFO 0x05u  // R: -> sys_ota_info_reply_t
typedef struct __attribute__((packed)) {
  uint8_t slot;         /**< The app slot it runs from: 0 or 1 */
  uint8_t state;        /**< sys_ota_state_e of that firmware */
  uint8_t open;         /**< 1: a write is in progress */
  uint32_t written;     /**< Its bytes so far */
  uint32_t slot_bytes;  /**< The largest image the other slot takes */
} sys_ota_info_reply_t;

/**
 * @brief The firmware update, under ROUTER_KEY_OTA.
 * Ops and layouts: SYS_OTA.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_OTA, sys_ota_router));
 */
ROUTER_MODULE_DECLARE(sys_ota_router);

#ifdef __cplusplus
}
#endif
