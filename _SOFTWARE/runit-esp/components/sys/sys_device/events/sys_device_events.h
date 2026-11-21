#pragma once

#include "sys_device_errors.h"
#include "sys_event.h"

/*
 * Events of the device registry (module MODULE_ID_SYS_DEVICE, 0x2F, the registry's number and the ID of its lifecycle
 * ops): what happened to a device. Each is a one-time change, not a state the device stays in (the `suspended` flag is
 * the state). The registry publishes them, after the lock of the device is let go, so a listener may call the registry.
 *
 * Source = the device, or SYS_DEVICE_ID_NONE for a sweep (sys_device_sweep of SUSPEND / RESUME / RESET: one event for
 * the whole sweep, not one per device; a REMOVE sweep publishes each device's REMOVED). Channel = 0.
 *
 * NOTIFY: the app learns of a safe state, a fault response or a reset without asking. CREATED and REMOVED only drive
 * code (the error policy starts a new device at its defaults and forgets a removed one; sys_device_remote follows the
 * remote devices; a module that keeps state on a device follows it).
 *
 *   SUSPENDED: the device went into the safe state
 *   RESUMED:   it left it
 *   RESET:     a RESET of the device's type worked, its settings are the defaults again
 *   CREATED:   the device exists
 *   REMOVED:   the device is gone, its ID is free again
 *
 * CREATED and REMOVED carry the slot's generation (`gen`: it counts the CREATEs of the ID, 1..255). Two tasks publish
 * after they let the lock go, so a REMOVED may reach a listener after the CREATED of the next device with the same ID:
 * a listener that keeps state per device keeps the generation of its CREATED and takes a REMOVED only with that
 * generation.
 */
#define SYS_DEVICE_EVENTS(X, P)                             \
  X(P, SUSPENDED, 0x01, SYS_EVENT_NOTIFY, {})               \
  X(P, RESUMED,   0x02, SYS_EVENT_NOTIFY, {})               \
  X(P, RESET,     0x03, SYS_EVENT_NOTIFY, {})               \
  X(P, CREATED,   0x04, 0,                { uint8_t gen; }) \
  X(P, REMOVED,   0x05, 0,                { uint8_t gen; })

SYS_EVENTS_DECLARE(SYS_DEVICE, MODULE_ID_SYS_DEVICE, DEVICE)
