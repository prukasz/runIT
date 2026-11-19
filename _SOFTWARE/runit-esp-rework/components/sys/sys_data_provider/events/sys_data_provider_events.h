#pragma once

#include "sys_data_provider_id.h"
#include "sys_event.h"

/*
 * Events of the providers (module MODULE_ID_PROVIDER, 0x0D): what happened to a provider and to its line. A provider's
 * data are bytes of the data matrix; what is not bytes (it exists, it is gone, its line is up or down) is an event.
 *
 * Source = the provider's id. Channel = 0.
 *
 * NOTIFY: the app learns when a line comes up or goes down (a BLE client connects, a UART is unplugged) without
 * asking. CREATED and REMOVED only drive code (a module that keeps state on a provider follows it).
 *
 *   CREATED: the provider exists, its port is in the data matrix; its type and state bits
 *   REMOVED: the provider is gone (its port out of the data matrix, its driver removed), its id is free again
 *   UP:      its line delivers frames now (SYS_DATA_PROVIDER_STATE_UP set)
 *   DOWN:    its line delivers none any more (SYS_DATA_PROVIDER_STATE_UP cleared)
 *
 * Defined only: the registry does not publish them yet (SYS_DATA_PROVIDER.MD "Events").
 */
#define SYS_DATA_PROVIDER_EVENTS(X, P)                                      \
  X(P, CREATED, 0x01, 0,                { uint8_t type; uint8_t state; }) \
  X(P, REMOVED, 0x02, 0,                { uint8_t type; })                \
  X(P, UP,      0x03, SYS_EVENT_NOTIFY, {})                               \
  X(P, DOWN,    0x04, SYS_EVENT_NOTIFY, {})

SYS_EVENTS_DECLARE(SYS_DATA_PROVIDER, MODULE_ID_PROVIDER, SYS)
