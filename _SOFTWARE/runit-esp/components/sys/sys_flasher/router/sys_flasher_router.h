#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_FLASHER (SYS_FLASHER.MD "Remote"), each above the frame it uses (packed, little-endian;
 * contract_flasher.json names them), with its sides. Never renumber. */

#define SYS_FLASHER_OP_START 0x01u  // W: -, flashing the board on the link starts; STATUS follows it
#define SYS_FLASHER_OP_ABORT 0x03u  // W: -, stops it between two packets

#define SYS_FLASHER_OP_STATUS 0x02u  // R: -> sys_flasher_status_reply_t
typedef struct __attribute__((packed)) {
  uint8_t state;   /**< sys_flasher_state_e */
  uint8_t step;    /**< sys_flasher_step_e */
  uint32_t done;   /**< Bytes written and checked */
  uint32_t total;  /**< Bytes to write */
  uint32_t error;  /**< FAILED: the tag of its error; else 0 */
} sys_flasher_status_reply_t;

/**
 * @brief Flashing the board on the link, under ROUTER_KEY_FLASHER.
 * Ops and layouts: SYS_FLASHER.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_FLASHER, sys_flasher_router));
 */
ROUTER_MODULE_DECLARE(sys_flasher_router);

#ifdef __cplusplus
}
#endif
