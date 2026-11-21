#pragma once

#include <stdint.h>

#include "router_table.h"
#include "sys_actions.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under the root key MODULE_ID_ACTIONS = 0x28 (SYS_ACTIONS.MD "Remote"), packed, little-endian;
 * contract_actions.json names them. Never renumber; 0x02, 0x03 and 0x0C are unused: never assign them. The router
 * registers itself (ROUTER_MODULE_REGISTER). */

/** @brief RUN / RUN_ALL / REMOVE / INFO args */
typedef struct __attribute__((packed)) {
  uint32_t id;  /**< The record's ID */
} sys_actions_id_args_t;

/** @brief BEGIN args */
typedef struct __attribute__((packed)) {
  uint32_t id;   /**< The record's ID */
  uint16_t len;  /**< The image's length in bytes */
} sys_actions_begin_args_t;

/** @brief WRITE args: the data follows */
typedef struct __attribute__((packed)) {
  uint16_t offset;  /**< The bytes written so far */
} sys_actions_write_args_t;

/** @brief COMMIT args */
typedef struct __attribute__((packed)) {
  uint32_t crc32;  /**< CRC-32 of the image */
} sys_actions_commit_args_t;

/** @brief READ args */
typedef struct __attribute__((packed)) {
  uint32_t id;      /**< The record's ID */
  uint16_t offset;  /**< Byte to start from; ask again from offset + the bytes got */
} sys_actions_read_args_t;

/** @brief LIST args */
typedef struct __attribute__((packed)) {
  uint32_t start;  /**< The ID to list from; ask again from the last ID + 1 */
} sys_actions_list_args_t;

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h). Sides: RUN W, BEGIN W, WRITE W
 * (the value is the head, the data follows), COMMIT W, READ R (answer: the image from offset, as much as fits, empty at
 * the end), INFO R, LIST R (answer: u16 total, then u32 IDs ascending from start, as many as fit), REMOVE W, ABORT W,
 * RUN_ALL W (RUN that runs every frame), REMOVE_ALL W.
 */
#define ACTIONS_OPERATIONS(X, P)                                                         \
  X(P, OP_RUN,        0x01, cmd_none_t,              sys_actions_id_args_t,     0)       \
  X(P, OP_RUN_ALL,    0x02, cmd_none_t,              sys_actions_id_args_t,     0)       \
  X(P, OP_BEGIN,      0x03, cmd_none_t,              sys_actions_begin_args_t,  0)       \
  X(P, OP_WRITE,      0x04, cmd_none_t,              sys_actions_write_args_t,  0)       \
  X(P, OP_COMMIT,     0x05, cmd_none_t,              sys_actions_commit_args_t, 0)       \
  X(P, OP_ABORT,      0x06, cmd_none_t,              cmd_none_t,                0)       \
  X(P, OP_READ,       0x07, sys_actions_read_args_t, cmd_none_t,                0)       \
  X(P, OP_INFO,       0x08, sys_actions_id_args_t,   sys_actions_info_t,        0)       \
  X(P, OP_LIST,       0x09, sys_actions_list_args_t, cmd_none_t,                0)       \
  X(P, OP_REMOVE,     0x0A, cmd_none_t,              sys_actions_id_args_t,     0)       \
  X(P, OP_REMOVE_ALL, 0x0B, cmd_none_t,              cmd_none_t,                0)

CMD_TABLE_DECLARE(ACTIONS)

/** @brief The stored records and their upload, under the root key MODULE_ID_ACTIONS. */
ROUTER_MODULE_DECLARE(sys_actions_router);

#ifdef __cplusplus
}
#endif
