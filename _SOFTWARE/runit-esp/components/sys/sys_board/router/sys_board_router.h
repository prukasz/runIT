#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_BOARD (SYS_BOARD.MD "Remote"), each above the frame it uses (packed, little-endian;
 * contract_board.json names them), with its sides. Never renumber. */

#define SYS_BOARD_OP_JSON_APPEND 0x01u  // W: sys_board_append_args_t + data
typedef struct __attribute__((packed)) {
  uint32_t offset;  /**< The file's length; the data follows */
} sys_board_append_args_t;

#define SYS_BOARD_OP_JSON_ERASE 0x02u  // W: -

#define SYS_BOARD_OP_JSON_INFO 0x03u  // R: -> sys_nvs_json_info_t { u32 bytes, u32 crc32, u16 chunks }

#define SYS_BOARD_OP_JSON_READ 0x04u  // R: sys_board_read_args_t -> the chunk's bytes
typedef struct __attribute__((packed)) {
  uint16_t chunk;  /**< 0..chunks-1 */
} sys_board_read_args_t;

/**
 * @brief The board's JSON descriptor file, under ROUTER_KEY_BOARD.
 * Ops and layouts: SYS_BOARD.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_BOARD, sys_board_router));
 */
ROUTER_MODULE_DECLARE(sys_board_router);

#ifdef __cplusplus
}
#endif
