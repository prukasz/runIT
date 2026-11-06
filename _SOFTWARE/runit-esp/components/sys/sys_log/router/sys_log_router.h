#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_LOG (SYS_LOG.MD "Remote"), each above the frame it uses (packed, little-endian;
 * contract_log.json names them), with its sides. Never renumber; 0x02 is unused: never assign it. */

/** @brief The stored record SYS_ACTIONS_ID_LOG holds the write frame: [ROUTER_KEY_LOG][CMD_DIR_WRITE][CONFIG][sys_log_cfg_t]. */
#define SYS_LOG_OP_CONFIG 0x01u  // R: -> sys_log_cfg_t; W: sys_log_cfg_t
typedef struct __attribute__((packed)) sys_log_cfg {
  uint8_t level;          /**< Log level (0=None, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Verbose) */
  uint8_t mirror_serial;  /**< 1 = mirror to console UART, 0 = disabled */
  uint8_t trace_errors;   /**< 1 = print error chains as log lines, 0 = disabled */
  uint8_t send_errors;    /**< 1 = send error records on errors stream, 0 = disabled */
  uint8_t send_logs;      /**< 1 = send log lines on logs stream, 0 = disabled */
} sys_log_cfg_t;

/* One kept error record: the answer is [total u8][record], only [total] for an index past the end. The app asks index 0
 * up to total - 1 (the oldest first). */
#define SYS_LOG_OP_ERRORS 0x03u  // R: sys_log_errors_args_t -> [total u8][error record]
typedef struct __attribute__((packed)) {
  uint8_t index;  /**< 0 = the oldest kept */
} sys_log_errors_args_t;

/**
 * @brief The log options and the error history, under ROUTER_KEY_LOG.
 * Ops and layouts: SYS_LOG.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_LOG, sys_log_router));
 */
ROUTER_MODULE_DECLARE(sys_log_router);

#ifdef __cplusplus
}
#endif
