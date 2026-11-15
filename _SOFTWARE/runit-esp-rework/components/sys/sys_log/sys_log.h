#pragma once

#include "cmd.h"
#include "data_matrix.h"
#include "errors.h"
#include "router_queue.h"
#include "sys_log_errors.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log output control (SYS_LOG.MD): what ESP_LOG prints, where, and whether a reported error chain is also
 * printed. Five options, applied at boot from the defaults and changed with CONFIG.
 *   level          how much is printed: 0 blocks every ESP_LOG line, up to CONFIG_LOG_MAXIMUM_LEVEL;
 *   mirror_serial  1: lines also go to the console (UART / USB-Serial-JTAG),
 *                  0: not to the console (a line still reaches LOG_STREAM_LOGS when send_logs is 1);
 *   trace_errors   1: a reported error chain is also printed as log lines,
 *                  0: only its error record is sent (LOG_STREAM_ERRORS);
 *   send_errors    1: a reported error chain is sent as a record on LOG_STREAM_ERRORS,
 *                  0: nothing is sent (the chain may still be printed, see trace_errors);
 *   send_logs      1: every log line is also sent as text to LOG_STREAM_LOGS,
 *                  0: log lines stay off the stream.
 * The session's binary frames on the console and the error records don't go through ESP_LOG, so no setting here can
 * silence them.
 */

/* The streams the module sends on (DATA_MATRIX.MD "Streams of a module"): the log lines, and the error records (urgent).
   What they reach is the matrix's routes. */
#define LOG_STREAMS(X, P)                                                                      X(P, LOGS,   0x03, 0,                  "the log lines, as text")                             X(P, ERRORS, 0x04, DATA_MATRIX_URGENT, "the error records of every reported chain")
DATA_MATRIX_STREAMS_DECLARE(LOG)

/** @brief The five options: the frame of CONFIG, defined in sys_log_router.h. */
typedef struct sys_log_cfg sys_log_cfg_t;

/**
 * @brief Tells the matrix the module's streams (LOG_STREAM_LOGS, LOG_STREAM_ERRORS: urgent) and takes over ESP_LOG: every line goes to
 * the console (if mirror_serial) and, without ANSI colours and the line end, cut to CONFIG_SYS_LOG_LINE_MAX, to
 * LOG_STREAM_LOGS; then applies the defaults (INFO, mirror off, trace on, send errors on, send logs on).
 * @return The error of the hook or of the defaults
 */
MUST_USE err_h sys_log_init(void);

/**
 * @brief Applies the options at once (not stored: the app saves the CONFIG write frame as a record).
 * @param[in] cfg The five options
 * @return ERR_LOG_BAD_LEVEL{level, max}, ERR_CMD_BAD_ARG for a flag other than 0 / 1 (nothing changes)
 */
MUST_USE err_h sys_log_set(const sys_log_cfg_t* cfg);

/**
 * @brief The options in force.
 * @param[out] out The five options
 */
void sys_log_get(sys_log_cfg_t* out);

/** @brief The largest error record kept (and sent on LOG_STREAM_ERRORS): longer ones are cut by err_record to this. */
#define SYS_LOG_RECORD_MAX 192u

/**
 * @brief An error record (cmd_err_record / err_record): kept in the history read by ERRORS (the latest
 * CONFIG_SYS_LOG_ERRORS_KEPT, the oldest dropped) and, with send_errors on, sent on LOG_STREAM_ERRORS. Any task; never
 * reports (the error sink calls it for every reported chain).
 * @param[in] record The record
 * @param[in] len    Its size in bytes
 */
void sys_log_error(const uint8_t* record, uint32_t len);

/**
 * @brief A reported error chain as a record (cmd_err_record, `cmd` the command it came from, or NULL): sys_log_error
 * of it. The error sink's call. Any task; never reports.
 * @param[in] chain The chain
 * @param[in] cmd   The command it came from, or NULL
 */
void sys_log_report(err_h chain, const cmd_req_t* cmd);

/**
 * @brief The kept record at `index` copied to `out`.
 * @param[in]  index 0 = the oldest
 * @param[out] out   Buffer of SYS_LOG_RECORD_MAX bytes
 * @param[out] total How many records are kept
 * @return The record's length; 0 past the end
 */
uint32_t sys_log_error_get(uint32_t index, uint8_t* out, uint32_t* total);

#ifdef __cplusplus
}
#endif
