#pragma once

#include "errors.h"

/* The board's way to the outside (RUNIT.MD "Connectivity"): the system streams and the ways it always has. */

/**
 * @brief The system streams: makes TELEMETRY (the VM will feed it) and EVENTS, starts sys_stream, starts sys_log (its
 * LOGS and ERRORS streams) and sends the SYS_EVENT_NOTIFY events to EVENTS. After sys_session_init and sys_event_init.
 */
MUST_USE err_h runit_streams_init(void);

/** @brief The console and the BLE pair (its characteristics made by the provider), then BLE started; each provider attached `UNKNOWN` (not told: the hello says a client or the master). */
MUST_USE err_h runit_ways_init(void);
