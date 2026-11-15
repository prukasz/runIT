#pragma once

#include "sys_event.h"
#include "sys_session_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Events of the sessions: module MODULE_ID_SESSION (0x0E, declared in sys_session_errors.h), source the peer's id,
 * channel 0. They run inline, on the context that noticed it (a driver's RX task, the board timers' task, the caller of
 * sys_session_detach): a listener drops what it keeps for that peer and never blocks.
 *   REACHED - a session opened with the peer (its hello came)
 *   GONE    - the session with the peer ended (the dead-man's silence, the provider down, a new boot, NO_SESSION,
 *             detach); `safety` 1: it had the safety flag (sys_system's safe state answers it)
 * @usage ERR_TRY(sys_event_subscribe(SESSION_GONE_FILTER, on_gone, NULL, SYS_EVENT_RUN_INLINE, &handle));
 */
#define SESSION_EVENTS(X, P)        \
  X(P, REACHED, 0x01, 0, {})        \
  X(P, GONE,    0x02, 0, { uint8_t safety; })

SYS_EVENTS_DECLARE(SESSION)

/** @brief Every peer's REACHED / GONE, for sys_event_subscribe. */
#define SESSION_REACHED_FILTER \
  ((sys_event_key_t){.tag = SESSION_EV_REACHED, .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL})
#define SESSION_GONE_FILTER \
  ((sys_event_key_t){.tag = SESSION_EV_GONE, .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL})

#ifdef __cplusplus
}
#endif
