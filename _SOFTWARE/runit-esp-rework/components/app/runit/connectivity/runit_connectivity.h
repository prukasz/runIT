#pragma once

#include "data_matrix.h"
#include "errors.h"

/* The board's way to the outside (RUNIT.MD "Connectivity"): the system streams and the ways it always has. */

/* The streams runit sends on, besides sys_log's LOGS and ERRORS (DATA_MATRIX.MD "Streams of a module"). */
#define RUNIT_STREAMS(X, P) \
  X(P, EVENTS, 0x06, DATA_MATRIX_URGENT, "the event notifications, as sys_event_t")
DATA_MATRIX_STREAMS_DECLARE(RUNIT)

/**
 * @brief The system streams: starts the data matrix, starts sys_log (its LOGS and ERRORS streams) and sends the
 * SYS_EVENT_NOTIFY events to EVENTS. After sys_session_init and sys_event_init.
 */
MUST_USE err_h runit_streams_init(void);

/**
 * @brief The console and the BLE pair (its characteristics made by the provider), then BLE started; their roles
 * (a session each) and the routes of the system streams to them. After the matrix and the modules that listen.
 */
MUST_USE err_h runit_ways_init(void);
