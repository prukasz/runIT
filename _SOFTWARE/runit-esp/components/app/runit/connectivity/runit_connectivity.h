#pragma once

#include "errors.h"

/* The board's way to the outside (RUNIT.MD "The board's data"). */

/**
 * @brief The board's data: starts the data matrix and sys_log (their streams), forwards the notify events to the
 * stream EVENTS, makes the board's own providers (the console, the runIT BLE pair, the BLE logs), gives them their roles
 * and routes (the tables of data_matrix_setup.c) and starts BLE. After sys_session_init and sys_event_init.
 */
MUST_USE err_h runit_data_matrix_setup(void);
