#pragma once

/**
 * @file err_sink.h
 * @brief Where reported chains go besides the log (err_sink.c): the sink the app sets at boot, and the report that
 * carries more than err_report - a command's chain. A chain about another board is reported like any other: it holds
 * the board (ERR_REMOTE{board}, or the tag's own field).
 */

#include <stdint.h>
#include "cmd_types.h"
#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Where reported chains go besides the log: the app's sink (the device error policy, the errors stream, the
 * history, the subscribers of another board). Called by err_report / err_report_ex before the chain is freed; must not
 * report errors itself (err_log + err_free only).
 * @param chain  The chain, borrowed: freed by the reporter after the call.
 * @param cmd    The command the chain belongs to (cmd_types.h), NULL for an internal error. Valid for the call only.
 */
typedef void (*err_sink_f)(err_h chain, const cmd_req_t* cmd);

/**
 * @brief Sets the sink. Boot only, before the first report.
 * @param fn  The sink; NULL: chains are only logged.
 */
void err_set_sink(err_sink_f fn);

/**
 * @brief err_report of a command's chain: logs it with `log_ctx` first, passes it to the sink with `cmd`, frees it.
 * Task context only.
 * @param chain    The chain, may be NULL (ignored). Owned: freed here.
 * @param log_ctx  The line logged before the chain (the command that failed), may be NULL.
 * @param cmd      The command the chain belongs to, may be NULL (an internal error).
 */
void err_report_ex(err_h chain, const char* log_ctx, const cmd_req_t* cmd);

#ifdef __cplusplus
}
#endif
