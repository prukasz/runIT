#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_SYS (SYS_SYSTEM.MD "Remote"), each above the frame it uses (packed, little-endian;
 * contract_sys.json names them), with its sides. Never renumber; 0x05 and 0x08 are unused: never assign them. Whether
 * a command runs urgent is the client's choice (ROUTER_FLAG_URGENT). */

#define SYS_SYSTEM_OP_PING    0x01u  // R: -> u32 uptime_ms
#define SYS_SYSTEM_OP_STOP    0x02u  // W: -, the safe state (reason STOP); the root's dead-man sends it to every board
#define SYS_SYSTEM_OP_RESTART 0x03u  // W: -, restarts the board ~100 ms after the answer (the hard reset)

#define SYS_SYSTEM_OP_DEADMAN 0x04u  // R: -> sys_system_deadman_t; W: sys_system_deadman_t
typedef struct __attribute__((packed)) {
  uint32_t timeout_ms;  /**< The clients' sessions' timeout in ms; their end is the safe state (0 = off) */
} sys_system_deadman_t;

#define SYS_SYSTEM_OP_BYE 0x06u  // W: -, the client leaves on purpose: the dead-man is disarmed

#define SYS_SYSTEM_OP_TIME 0x07u  // R: -> sys_system_time_t; W: sys_system_time_t, the root sends its own to every board
typedef struct __attribute__((packed)) {
  uint64_t unix_ms;  /**< UTC, milliseconds since 1970-01-01; read: 0 = no client set it since the power came on */
} sys_system_time_t;

#define SYS_SYSTEM_OP_SAFE 0x09u  // R: -> sys_system_safe_t, the safe state: why it was entered and how long ago
typedef struct __attribute__((packed)) {
  uint8_t reason;   /**< sys_safe_reason_e of the last entry; 0 = not in the safe state */
  uint32_t for_ms;  /**< Time since that entry */
} sys_system_safe_t;

#define SYS_SYSTEM_OP_SAFE_LEAVE 0x0Au  // W: -, every device resumed (the application's leave), the note cleared

/** @brief Router of ROUTER_KEY_SYS: ping, stop, restart, dead-man, bye, clock, safe state (SYS_SYSTEM.MD "Remote"). */
ROUTER_MODULE_DECLARE(sys_system_router);

#ifdef __cplusplus
}
#endif
