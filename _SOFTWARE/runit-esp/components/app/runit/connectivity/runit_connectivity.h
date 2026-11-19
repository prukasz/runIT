#pragma once

#include "errors.h"

/* The board's way to the outside (RUNIT.MD "The board's data"). */

/**
 * @brief The board's data: the ports of sys_log (LOGS, ERRORS) and of the events (EVENTS), the board's own providers
 * (the console, the runIT BLE pair, the raw BLE logs and telemetry), the sessions of the console (everything) and the
 * BLE pair (commands, errors, events) with their sub-ports and links, LOGS and WATCH to the raw characteristics, then
 * BLE (the tables of data_matrix_setup.c). After sys_session_init,
 * sys_event_init, router_init, sys_watch_init and sys_remote_init (their ports are linked here).
 */
MUST_USE err_h runit_data_matrix_setup(void);
