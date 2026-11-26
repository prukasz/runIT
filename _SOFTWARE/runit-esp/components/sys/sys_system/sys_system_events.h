#pragma once

#include "sys_event.h"
#include "sys_system_id.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Events of the system (module MODULE_ID_SYS, lane SYS).
 *   SAFE - the board enters the safe state (sys_system_enter_safe_state); channel = the reason (sys_safe_reason_e),
 *          source = the device whose error caused it (reason FAULT), else SYS_DEVICE_ID_NONE. Published inline before
 *          the devices are suspended, so a listener stops what writes to them first (the VM stops its program). A
 *          listener runs on the task that enters the safe state (a router task, the urgent task, the device policy's,
 *          the power manager's): it only asks for its stop and never waits for it.
 *   SAFE_LEFT - the board left the safe state (sys_system_leave_safe_state: every device resumed); channel = the
 *          reason it was in, source SYS_DEVICE_ID_NONE. The program stays stopped: VM EXEC starts it.
 * @usage ERR_TRY(sys_event_subscribe(SYS_SAFE_FILTER, on_safe, NULL, SYS_EVENT_RUN_INLINE, &handle));
 */
#define SYS_EVENTS(X, P)                      \
  X(P, SAFE,      0x01, SYS_EVENT_NOTIFY, {}) \
  X(P, SAFE_LEFT, 0x02, SYS_EVENT_NOTIFY, {})

SYS_EVENTS_DECLARE(SYS, MODULE_ID_SYS, SYS)

/** @brief Every SAFE, whatever the reason and the source, for sys_event_subscribe. */
#define SYS_SAFE_FILTER \
  ((sys_event_key_t){.tag = SYS_EV_SAFE, .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL})

#ifdef __cplusplus
}
#endif
