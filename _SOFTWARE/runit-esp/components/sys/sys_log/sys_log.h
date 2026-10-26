#pragma once

#include "cmd.h"
#include "errors.h"
#include "utils.h"
#include "sys_log_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log output control (SYS_LOG.MD): what ESP_LOG prints, where, and whether
 * a reported error chain is also printed. Five options, applied at boot from the
 * defaults and changed with SET. They are kept as a record (SYS_ACTIONS_ID_LOG, the SET frame, written by the app)
 * that whoever starts the project runs; nothing replays by itself:
 *   level          how much is printed: 0 blocks every ESP_LOG line, up to
 *                  CONFIG_LOG_MAXIMUM_LEVEL (the compile-time ceiling);
 *   mirror_serial  1: lines also go to the console (UART / USB-Serial-JTAG),
 *                  0: not to the console (a line still reaches the logs connector when send_logs is 1);
 *   trace_errors   1: a reported error chain is also printed as log lines,
 *                  0: only its error record is sent (the errors stream).
 *   send_errors    1: a reported error chain is sent as a record on the errors stream,
 *                  0: nothing is sent (the chain may still be printed, see trace_errors);
 *   send_logs      1: every log line is also sent as text to the logs connector (BLE),
 *                  0: log lines stay on the console.
 * The command link ("#R:" lines) and the error records don't go through ESP_LOG,
 * so no setting here can silence them.
 */

/** @brief Ops of ROUTER_KEY_LOG. The stored record SYS_ACTIONS_ID_LOG holds the SET frame: [ROUTER_KEY_LOG][SET][sys_log_cfg_t]. */
#define SYS_LOG_OP_CONFIG 0x01u  // read: -> sys_log_cfg_t; write: sys_log_cfg_t
#define SYS_LOG_OP_ERRORS 0x03u  // [index u8] -> [total u8][record], the error history (oldest is index 0)

/** @brief The settings record and the SET / GET payload (packed, the wire form). */
typedef struct __attribute__((packed)) {
  uint8_t level;          /**< Log level (0=None, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Verbose) */
  uint8_t mirror_serial;  /**< 1 = mirror to console UART, 0 = disabled */
  uint8_t trace_errors;   /**< 1 = print error chains as log lines, 0 = disabled */
  uint8_t send_errors;    /**< 1 = send error records on errors stream, 0 = disabled */
  uint8_t send_logs;      /**< 1 = send log lines on logs stream, 0 = disabled */
} sys_log_cfg_t;

/**
 * @brief Takes over ESP_LOG: every line goes to the console (if mirror_serial) and, without ANSI colours and the line
 * end, cut to CONFIG_SYS_LOG_LINE_MAX, to connector `connector`; then applies the defaults (INFO,
 * mirror on, trace on, send errors on, send logs on). Boot step, after sys_connector_init().
 */
MUST_USE err_h sys_log_init(uint8_t connector);

/**
 * @brief Applies the options at once (not stored: the app saves the SET frame as a record).
 * @return ERR_LOG_BAD_LEVEL{level, max}, ERR_CMD_BAD_ARG for a flag other than 0 / 1 (nothing changes).
 */
MUST_USE err_h sys_log_set(const sys_log_cfg_t* cfg);

/** @brief The options in force. */
void sys_log_get(sys_log_cfg_t* out);

/** @brief The largest error record kept (and sent on the errors stream): longer ones are cut by err_record to this. */
#define SYS_LOG_RECORD_MAX 192u

/**
 * @brief An error record (err_record): kept in the history the app reads with ERRORS (the latest
 * CONFIG_SYS_LOG_ERRORS_KEPT, the oldest dropped) and, with send_errors on, sent on the errors stream
 * (SYS_CONNECTOR_ERRORS). Any task; never reports (the error sink calls it for every error).
 */
void sys_log_error(const uint8_t* record, uint32_t len);

/** @brief The kept record at `index` (0 = the oldest) copied to `out` (capacity SYS_LOG_RECORD_MAX); its length, 0 past the end. `total` is how many are kept. */
uint32_t sys_log_error_get(uint32_t index, uint8_t* out, uint32_t* total);

#ifdef __cplusplus
}
#endif
