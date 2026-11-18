#pragma once

/**
 * @file err_log.h
 * @brief A chain as log text (err_log.c). err_report logs by itself: this is for the few that print a chain they do
 * not report (a sink, which must not report), for a context line before the chain, and for switching the lines off.
 */

#include <stdbool.h>
#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Prints a chain to the log, outermost node first, one line per node: `[i] NAME (MODULE.code) SEV`, the
 * payload (an ESP-IDF code by name, anything else in hex) and `@ file:line func()` where call-site info is on
 * (CONFIG_ERRORS_INFO). A tag this board does not know prints as `unknown tag 0x...`. The whole chain takes one log
 * level, from err_chain_sev: HIGH and CRITICAL error, MEDIUM warning, else info. A line longer than
 * CONFIG_ERRORS_LOG_LINE_LEN is cut. Borrows the chain: does not free it. Task context only (ESP_LOG).
 * @param chain  The chain, may be NULL (ignored).
 */
void err_log(err_h chain);

/**
 * @brief err_log with a context line first (the command that failed, "board"), at the chain's level.
 * @param chain    The chain, may be NULL (ignored: no line at all).
 * @param context  The line printed before the chain; NULL is err_log.
 */
void err_log_ctx(err_h chain, const char* context);

/**
 * @brief Switches the log lines of chains (err_log, err_log_ctx, so also every report) on or off; on at boot. A
 * reported chain still reaches the sink and is freed: only the text stops. The level of the lines is ESP_LOG's own.
 * @param on  true: the lines are printed.
 */
void err_log_enable(bool on);

#ifdef __cplusplus
}
#endif
