#pragma once

/**
 * @file err_log.h
 * @brief A chain as log text (err_log.c). err_report logs by itself: this is for the few that print a chain they do
 * not report (a sink, which must not report) and for switching the lines off.
 */

#include <stdbool.h>
#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Prints a chain to the log, outermost node first, one line per node:
 * tag name, module.code, severity, payload in hex, and file:line func() where
 * the info is enabled.
 * The whole chain uses one log level, from err_chain_sev (HIGH/CRITICAL -
 * error, MEDIUM - warning, else info). Does not free the chain. Task context
 * only (uses ESP_LOG). NULL is ignored.
 */
void err_log(err_h chain);

/**
 * @brief Switches the log lines of chains (err_log, so also err_report) on or off; on at boot. A chain
 * still reaches the sink and is freed: only the text stops. The level of the lines is ESP_LOG's own.
 */
void err_log_enable(bool on);

#ifdef __cplusplus
}
#endif
