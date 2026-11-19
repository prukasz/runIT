#pragma once

#include "errors.h"

/* The board's way to the outside (RUNIT.MD "The board's data"). */

/**
 * @brief The board's data: the board's own providers (the console, the runIT BLE pair, the raw BLE logs and
 * telemetry), the sessions of the console (everything) and the BLE pair (commands, errors, events) with their sub-ports
 * and links, LOGS and WATCH to the raw characteristics, then BLE (the tables of data_matrix_setup.c). After the modules
 * whose ports it links: router_init, sys_log_init, sys_event_stream_init, sys_watch_init, sys_remote_init.
 */
MUST_USE err_h runit_data_matrix_setup(void);
