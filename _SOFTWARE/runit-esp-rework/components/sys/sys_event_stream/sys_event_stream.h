#pragma once

#include "data_matrix.h"
#include "errors.h"

/**
 * @file sys_event_stream.h
 * @brief The events as a stream of the data matrix (SYS_EVENT_STREAM.MD), both ways: the events that matter to the user
 * (SYS_EVENT_NOTIFY) go out on EVENT_STREAM_NOTIFY as the sys_event_t they are, and the sys_event_t that come in on
 * EVENT_STREAM_REMOTE are published here. Where each goes and comes from is the matrix's routes: the app on a session, the
 * events of a slave from its UART (a bridge), a log.
 * @usage ERR_TRY(sys_event_stream_init());
 *        ERR_TRY(data_matrix_route(&(data_matrix_route_t){DATA_MATRIX_END_STREAM(EVENT_STREAM_NOTIFY),
 *                                                          DATA_MATRIX_END_TAGGED(app_session, EVENT_STREAM_NOTIFY)}));
 */

#ifdef __cplusplus
extern "C" {
#endif

/* The module's streams (DATA_MATRIX.MD "Streams of a module"). */
//#ref-streams event_stream @alias Event streams @prefix EVENT_STREAM_
#define EVENT_STREAMS(X, P)                                                                                    \
  X(P, NOTIFY, 0x06, DATA_MATRIX_URGENT, "this board's notify events, sys_event_t")                               \
  X(P, REMOTE, 0x07, 0,                  "events of other boards, sys_event_t: published here without NOTIFY")
DATA_MATRIX_STREAMS_DECLARE(EVENT)

/**
 * @brief Starts it: gives the matrix the streams' flags, subscribes the listener that sends each event with
 * SYS_EVENT_NOTIFY on EVENT_STREAM_NOTIFY (first in the table, SYS_EVENT_RUN_FIRST: an action that stops the board can't
 * hide its cause), and makes the sink that publishes what comes in on EVENT_STREAM_REMOTE. Boot, after sys_event_init and
 * data_matrix_init.
 * @return NULL; the error of the subscription or of the matrix's tables
 */
MUST_USE err_h sys_event_stream_init(void);

#ifdef __cplusplus
}
#endif
