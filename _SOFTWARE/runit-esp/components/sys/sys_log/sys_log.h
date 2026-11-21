#pragma once

#include "cmd.h"
#include "err_record.h"
#include "errors.h"
#include "sys_log_id.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log output control (SYS_LOG.MD): what ESP_LOG prints, whether also to the console, and whether a reported
 * error chain is also printed. Three options, applied at boot from the defaults and changed with CONFIG.
 *   level          how much is printed: 0 blocks every ESP_LOG line, up to CONFIG_LOG_MAXIMUM_LEVEL;
 *   mirror_serial  1: lines also go to the console (UART / USB-Serial-JTAG), around the data matrix,
 *                  0: not to the console (a line still goes out of LOGS);
 *   trace_errors   1: a reported error chain is also printed as log lines,
 *                  0: not printed (its record is still kept and sent out of ERRORS).
 * Whether lines and records leave the board is their ports' links: a line is not even formatted while LOGS has none.
 * The session's binary frames on the console and the error records don't go through ESP_LOG, so no setting here can
 * silence them.
 */

/* The module's two ports of the data matrix (SYS_LOG.MD "Ports"), each packet [hdr PUSH, key][data]:
     LOGS    DATA_MATRIX_PORT(MODULE, MODULE_ID_LOG): the log lines as text, out only;
     ERRORS  DATA_MATRIX_PORT(MODULE, MODULE_ID_ERRORS): this board's error records out (urgent), other boards' in,
             passed on to every port linked from it but the one they came from.
   Where they go is their links. */

/** @brief The three options: the frame of CONFIG, defined in sys_log_router.h. */
typedef struct sys_log_cfg sys_log_cfg_t;

/**
 * @brief Registers the module's ports (LOGS, ERRORS) and takes over ESP_LOG: every line goes to the console (if
 * mirror_serial) and, without ANSI colours and the line end, cut to one frame (CMD_DATA_MAX), out of LOGS; then
 * applies the defaults (INFO, mirror off, trace on).
 * @return ERR_BAD_ARG / _FULL of a port's registration
 */
MUST_USE err_h sys_log_init(void);

/**
 * @brief Applies the options at once (not stored: the app saves the CONFIG write frame as a record).
 * @param[in] cfg The three options
 * @return ERR_RANGE{level, max}, ERR_BAD_ARG for a flag other than 0 / 1 (nothing changes)
 */
MUST_USE err_h sys_log_set(const sys_log_cfg_t* cfg);

/**
 * @brief The options in force.
 * @param[out] out The three options
 */
void sys_log_get(sys_log_cfg_t* out);

/**
 * @brief A reported error chain as a record (err_record_cmd, `cmd` the command it came from, or NULL, cut to
 * ERR_RECORD_MAX): kept in the history read by OP_ERRORS (the latest CONFIG_SYS_LOG_ERRORS_KEPT, the oldest
 * dropped) and sent out of ERRORS. The error sink's call. Any task; never reports.
 * @param[in] chain The chain
 * @param[in] cmd   The command it came from, or NULL
 */
void sys_log_report(err_h chain, const cmd_req_t* cmd);

/**
 * @brief The kept record at `index` copied to `out`.
 * @param[in]  index 0 = the oldest
 * @param[out] out   Buffer of ERR_RECORD_MAX bytes
 * @param[out] total How many records are kept
 * @return The record's length; 0 past the end
 */
uint32_t sys_log_error_get(uint32_t index, uint8_t* out, uint32_t* total);

#ifdef __cplusplus
}
#endif
