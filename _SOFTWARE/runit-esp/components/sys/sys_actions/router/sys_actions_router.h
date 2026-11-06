#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_ACTIONS (SYS_ACTIONS.MD "Remote"), each above the frame it uses (packed, little-endian;
 * contract_actions.json names them), with its sides. Never renumber; 0x02, 0x03 and 0x0C are unused: never assign them. */

#define SYS_ACTIONS_OP_RUN        0x01u  // W: sys_actions_id_args_t
#define SYS_ACTIONS_OP_RUN_ALL    0x0Du  // W: sys_actions_id_args_t, RUN that keeps going after a failing frame
#define SYS_ACTIONS_OP_REMOVE     0x0Au  // W: sys_actions_id_args_t
#define SYS_ACTIONS_OP_INFO       0x08u  // R: sys_actions_id_args_t -> sys_actions_info_t
typedef struct __attribute__((packed)) {
  uint32_t id;  /**< Action record ID */
} sys_actions_id_args_t;

#define SYS_ACTIONS_OP_BEGIN 0x04u  // W: sys_actions_begin_args_t
typedef struct __attribute__((packed)) {
  uint32_t id;   /**< Action record ID */
  uint16_t len;  /**< Length in bytes */
} sys_actions_begin_args_t;

#define SYS_ACTIONS_OP_WRITE 0x05u  // W: sys_actions_write_args_t + data
typedef struct __attribute__((packed)) {
  uint16_t offset;  /**< Byte offset in upload; the data follows */
} sys_actions_write_args_t;

#define SYS_ACTIONS_OP_COMMIT 0x06u  // W: sys_actions_commit_args_t
typedef struct __attribute__((packed)) {
  uint32_t crc32;  /**< CRC-32 of image */
} sys_actions_commit_args_t;

#define SYS_ACTIONS_OP_READ 0x07u  // R: sys_actions_read_args_t -> the image from offset, as many bytes as fit (empty at the end)
typedef struct __attribute__((packed)) {
  uint32_t id;      /**< Action record ID */
  uint16_t offset;  /**< Byte offset in the image; ask again from offset + the bytes got */
} sys_actions_read_args_t;

#define SYS_ACTIONS_OP_LIST 0x09u  // R: sys_actions_list_args_t -> u16 total, then u32 IDs ascending from start, as many as fit
typedef struct __attribute__((packed)) {
  uint32_t start;  /**< Action record ID to start listing from; ask again from the last ID + 1 */
} sys_actions_list_args_t;

#define SYS_ACTIONS_OP_ABORT      0x0Bu  // W: -
#define SYS_ACTIONS_OP_REMOVE_ALL 0x0Eu  // W: -, every stored record (sys_actions_remove_all); ERR_ACTION_BUSY while a draft is open

/**
 * @brief The action records and their upload, under ROUTER_KEY_ACTIONS.
 * Ops and layouts: SYS_ACTIONS.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_ACTIONS, sys_actions_router));
 */
ROUTER_MODULE_DECLARE(sys_actions_router);

#ifdef __cplusplus
}
#endif
