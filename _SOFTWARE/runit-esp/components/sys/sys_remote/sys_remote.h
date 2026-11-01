#pragma once

#include "errors.h"

/**
 * @file sys_remote.h
 * @brief Devices of other boards (SYS_REMOTE.MD): sets sys_device's remote hook, which carries a remote device's
 * writes as calls and its reads as cached reads, and turns its board's events into this device's. Nothing depends on
 * it: a board without other boards leaves it out.
 * @usage ERR_TRY(sys_remote_init());   // boot, after the session, the watches, the connectors, the events
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sets the remote hook of sys_device. Boot step, after sys_session, sys_watch, sys_data_connector and sys_event.
 * @return NULL when started.
 */
MUST_USE err_h sys_remote_init(void);

#ifdef __cplusplus
}
#endif
