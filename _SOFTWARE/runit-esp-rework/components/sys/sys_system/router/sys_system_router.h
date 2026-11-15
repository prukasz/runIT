#pragma once

#include <stdint.h>

#include "router_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The module's one number: the root key of its commands. */
MODULE_ID_DECLARE(SYS, 0x28)  //@alias System control @description System info, reboot, ping, and board diagnostics module ID

/* Ops under the root key MODULE_ID_SYS (SYS_SYSTEM.MD "Remote"), packed, little-endian; contract_sys.json names them.
 * Never renumber; 0x05 and 0x08 are unused: never assign them. Whether a command runs urgent is the client's choice
 * (ROUTER_FLAG_URGENT). The router registers itself (ROUTER_MODULE_REGISTER). */

/** @brief DEADMAN value */
typedef struct __attribute__((packed)) {
  uint32_t timeout_ms;  /**< The master's and the clients' sessions' timeout in ms; their end is the safe state (0 = off) */
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

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h). Sides: PING R (the uptime in
 * ms), STOP W (the safe state, reason STOP; the root's dead-man sends it to every board), RESTART W (the board
 * restarts RESTART_DELAY_MS after the answer), DEADMAN RW, BYE W (the client leaves on purpose: the dead-man is
 * disarmed), TIME RW (the root sends a time it is given to every board), SAFE R, SAFE_LEAVE W (every device resumed).
 */
#define SYS_OPERATIONS(X, P)                                               \
  X(P, OP_PING,       0x01, cmd_none_t, uint32_t,             0)           \
  X(P, OP_STOP,       0x02, cmd_none_t, cmd_none_t,           0)           \
  X(P, OP_RESTART,    0x03, cmd_none_t, cmd_none_t,           0)           \
  X(P, OP_DEADMAN,    0x04, cmd_none_t, sys_system_deadman_t, 0)           \
  X(P, OP_BYE,        0x06, cmd_none_t, cmd_none_t,           0)           \
  X(P, OP_TIME,       0x07, cmd_none_t, sys_system_time_t,    0)           \
  X(P, OP_SAFE,       0x09, cmd_none_t, sys_system_safe_t,    0)           \
  X(P, OP_SAFE_LEAVE, 0x0A, cmd_none_t, cmd_none_t,           0)

CMD_TABLE_DECLARE(SYS)

/** @brief Router of MODULE_ID_SYS: ping, stop, restart, dead-man, bye, clock, safe state. */
ROUTER_MODULE_DECLARE(sys_system_router);

#ifdef __cplusplus
}
#endif
