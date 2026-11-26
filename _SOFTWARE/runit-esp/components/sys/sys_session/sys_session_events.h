#pragma once

#include "sys_event.h"
#include "sys_session_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Events of the sessions: module MODULE_ID_SESSION (0x2C, declared in sys_session_errors.h), source the peer's id,
 * channel 0. They run inline, on the context that noticed it (a driver's RX task, the board timers' task, the caller of
 * sys_session_detach): a listener drops what it keeps for that peer and never blocks.
 *   REACHED - a session opened with the peer (its hello came)
 *   GONE    - the session with the peer ended (the dead-man's silence, the provider down, a new boot, NO_SESSION,
 *             detach); `safety` 1: it had the safety flag (sys_system's safe state answers it); `provider` its
 *             provider; `silent` 1: the dead-man's silence ended it (the peer is gone, its link may still be up)
 *   LEFT    - the peer leaves on purpose (sys_session_leave, its BYE); `provider` its session's provider
 * @usage ERR_TRY(sys_event_subscribe(SESSION_GONE_FILTER, on_gone, NULL, SYS_EVENT_RUN_INLINE, &handle));
 */
#define SESSION_EVENTS(X, P)                                                   \
  X(P, REACHED, 0x01, 0, {})                                                   \
  X(P, GONE,    0x02, 0, { uint8_t safety; uint8_t provider; uint8_t silent; }) \
  X(P, LEFT,    0x03, 0, { uint8_t provider; })

SYS_EVENTS_DECLARE(SESSION, MODULE_ID_SESSION, SYS)

/** @brief Every peer's REACHED / GONE / LEFT, for sys_event_subscribe. */
#define SESSION_REACHED_FILTER \
  ((sys_event_key_t){.tag = SESSION_EV_REACHED, .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL})
#define SESSION_GONE_FILTER \
  ((sys_event_key_t){.tag = SESSION_EV_GONE, .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL})
#define SESSION_LEFT_FILTER \
  ((sys_event_key_t){.tag = SESSION_EV_LEFT, .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL})

#ifdef __cplusplus
}
#endif
