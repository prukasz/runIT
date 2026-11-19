#pragma once

#include "data_matrix.h"
#include "errors.h"

/**
 * @file sys_event_stream.h
 * @brief The events as a port of the data matrix (SYS_EVENT_STREAM.MD), EVENTS =
 * DATA_MATRIX_PORT(MODULE, MODULE_ID_SYS_EVENT), both ways, each packet [hdr PUSH, MODULE_ID_SYS_EVENT][sys_event_t]:
 * the events that matter to the user (SYS_EVENT_NOTIFY) go out as they are; the events of other boards that come in
 * are passed on to every port linked from it but the one they came from, and published here without NOTIFY. Where
 * they go is its links: the app's session, a slave's session.
 * @usage ERR_TRY(sys_event_stream_init());
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts it: registers EVENTS and subscribes the listener that sends each event with SYS_EVENT_NOTIFY (first in
 * the table, SYS_EVENT_RUN_FIRST: an action that stops the board can't hide its cause). Boot, after sys_event_init.
 * @return NULL; the error of the registration or of the subscription
 */
MUST_USE err_h sys_event_stream_init(void);

#ifdef __cplusplus
}
#endif
