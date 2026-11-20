#pragma once

#include <stdint.h>

#include "sys_system_id.h"
#include "router_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under the root key MODULE_ID_SYS (SYS_SYSTEM.MD "Remote"), packed, little-endian; contract_sys.json names them.
 * Never renumber; 0x05 and 0x08 are unused: never assign them. Whether a command runs urgent is the client's choice
 * (ROUTER_FLAG_URGENT). The router registers itself (ROUTER_MODULE_REGISTER). */

/** @brief DEADMAN value */
typedef struct __attribute__((packed)) {
  uint32_t timeout_ms;  /**< The dead-man of the session the command came on: its time in ms; its end is the safe state (0 = off, the session never ends by silence) */
} sys_system_deadman_t;

/** @brief TIME value */
typedef struct __attribute__((packed)) {
  uint64_t unix_ms;  /**< UTC, milliseconds since 1970-01-01; read: 0 = no client set it since the power came on */
} sys_system_time_t;

/** @brief SAFE answer */
typedef struct __attribute__((packed)) {
  uint8_t reason;   /**< sys_safe_reason_e of the last entry; 0 = not in the safe state */
  uint32_t for_ms;  /**< Time since that entry */
} sys_system_safe_t;

/** @brief TASKS row: one task of the board (the answer: [u8 count][count rows]) */
typedef struct __attribute__((packed)) {
  char name[16];        /**< Its name, NUL-padded */
  uint16_t stack_free;  /**< The least stack it has had free since it started, in bytes */
  uint8_t priority;     /**< Its priority now */
} sys_system_task_t;

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h). Sides: PING R (the uptime in
 * ms), STOP W (the safe state, reason STOP; a dead-man's end sends it on every open session), RESTART W (the board
 * restarts RESTART_DELAY_MS after the answer), DEADMAN RW, BYE W (the client leaves on purpose: the dead-man is
 * disarmed), TIME RW (a time it is given goes on every open session), SAFE R, SAFE_LEAVE W (every device resumed),
 * TASKS R (every task's least free stack: [u8 count][sys_system_task_t ...]).
 */
#define SYS_OPERATIONS(X, P)                                               \
  X(P, OP_PING,       0x01, cmd_none_t, uint32_t,             0)           \
  X(P, OP_STOP,       0x02, cmd_none_t, cmd_none_t,           0)           \
  X(P, OP_RESTART,    0x03, cmd_none_t, cmd_none_t,           0)           \
  X(P, OP_DEADMAN,    0x04, cmd_none_t, sys_system_deadman_t, 0)           \
  X(P, OP_BYE,        0x06, cmd_none_t, cmd_none_t,           0)           \
  X(P, OP_TIME,       0x07, cmd_none_t, sys_system_time_t,    0)           \
  X(P, OP_SAFE,       0x09, cmd_none_t, sys_system_safe_t,    0)           \
  X(P, OP_SAFE_LEAVE, 0x0A, cmd_none_t, cmd_none_t,           0)           \
  X(P, OP_TASKS,      0x0B, cmd_none_t, sys_system_task_t,    0)

CMD_TABLE_DECLARE(SYS)

/** @brief Router of MODULE_ID_SYS: ping, stop, restart, dead-man, bye, clock, safe state, tasks. */
ROUTER_MODULE_DECLARE(sys_system_router);

#ifdef __cplusplus
}
#endif
