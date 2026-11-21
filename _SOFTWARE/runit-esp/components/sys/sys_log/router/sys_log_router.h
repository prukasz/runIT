#pragma once

#include "router_table.h"
#include "sys_log_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under root key MODULE_ID_LOG = 0x2A (SYS_LOG.MD "Remote"), packed, little-endian; contract_log.json names
 * them. Never renumber; 0x02 is unused: never assign it. The router registers itself (ROUTER_MODULE_REGISTER). */

/** @brief CONFIG args and answer. Stored record SYS_ACTIONS_ID_LOG holds [MODULE_ID_LOG][CMD_DIR_WRITE][OP_CONFIG][sys_log_cfg_t]. */
typedef struct __attribute__((packed)) sys_log_cfg {
  uint8_t level;          /**< Log level (0=None, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Verbose) */
  uint8_t mirror_serial;  /**< 1 = mirror to console UART, 0 = disabled */
  uint8_t trace_errors;   /**< 1 = print error chains as log lines, 0 = disabled */
} sys_log_cfg_t;

/** @brief ERRORS args */
typedef struct __attribute__((packed)) {
  uint8_t index;  /**< 0 = the oldest kept */
} sys_log_errors_args_t;

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h).
 * Sides: CONFIG RW, ERRORS R (answer: [u8 total][error record], or [u8 total] when past the end).
 */
#define LOG_OPERATIONS(X, P)                                            \
  X(P, OP_CONFIG, 0x01, cmd_none_t,            sys_log_cfg_t, 0)        \
  X(P, OP_ERRORS, 0x02, sys_log_errors_args_t, cmd_none_t,    0)

CMD_TABLE_DECLARE(LOG)

/** @brief The log options and the error history, under root key MODULE_ID_LOG. */
ROUTER_MODULE_DECLARE(sys_log_router);

#ifdef __cplusplus
}
#endif
