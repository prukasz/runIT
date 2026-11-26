#pragma once

#include "data_matrix.h"
#include "errors.h"

/**
 * @file sys_event_stream.h
 * @brief The events as a port of the data matrix (SYS_EVENT_STREAM.MD), EVENTS =
 * DATA_MATRIX_PORT(MODULE, MODULE_ID_SYS_EVENT), both ways, each packet [hdr PUSH, MODULE_ID_SYS_EVENT][sys_event_t]:
 * the events that matter to the user (SYS_EVENT_NOTIFY) go out as they are; the events of other boards that come in
 * are only passed on to every port linked from it but the one they came from, never published here (their ids are
 * the sender's). Where they go is its links: the app's session, a slave's session.
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif
