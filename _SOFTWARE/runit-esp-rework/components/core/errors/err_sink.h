#pragma once

/**
 * @file err_sink.h
 * @brief Where reported chains go besides the log (err_sink.c): the sinks the app sets at boot, and the reports that
 * carry more than err_report - a command's chain, a chain about another board.
 */

#include <stdint.h>
#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The command a chain belongs to (router_queue.h, cmd_req_t). errors only passes it on to the sink and never reads it: the
   record of a command is built by cmd_err_record() (router_queue.h), over err_record_cmd() (err_record.h). */
struct cmd_req;

/**
 * @brief Where reported chains go besides the log (the app sends them to the
 * errors stream). `cmd` is the command the chain belongs to, NULL for an
 * internal error. Must not report errors itself.
 */
typedef void (*err_sink_f)(err_h chain, const struct cmd_req* cmd);

/**
 * @brief Sets the sink. Boot only. NULL = log only.
 */
void err_set_sink(err_sink_f fn);

/**
 * @brief err_report of a command's chain: logs it with `log_ctx` first (may be NULL), passes it to the sink with
 * `cmd` (may be NULL), frees it. NULL is ignored. Task context only.
 */
void err_report_ex(err_h chain, const char* log_ctx, const struct cmd_req* cmd);

/** @brief Where a chain about another board goes besides the log (the app's policy per board and the errors stream). */
typedef void (*err_board_sink_f)(err_h chain, uint8_t board);

/** @brief Sets the board sink. Boot only. NULL = log only. */
void err_set_board_sink(err_board_sink_f fn);

/**
 * @brief Ends a chain about another board as a whole (a mirror of its stream lost frames, no room to follow it): logs
 * it, passes it to the board sink, frees it. NULL is ignored. Task context only.
 * @param chain  The chain.
 * @param board  The board's id.
 */
void err_report_board(err_h chain, uint8_t board);

#ifdef __cplusplus
}
#endif
