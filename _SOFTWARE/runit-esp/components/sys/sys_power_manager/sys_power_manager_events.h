#pragma once

#include <stdint.h>

#include "sys_event.h"
#include "sys_power_contract.h"
#include "sys_power_manager_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Events of the power manager itself: module MODULE_ID_PWRM (0x33), source SYS_DEVICE_ID_NONE (or the rail device for
 * LIMIT_REACHED), channel 0. Fault/matrix event codes match sys_power_kind_e; informational events start at 0x10.
 *   INPUT_OVP        - the input voltage is above its limit (max_mV and the manager's margin)
 *   BATTERY_LOW      - the battery fell to its low threshold
 *   BATTERY_CRITICAL - the battery fell to its critical threshold
 *   BUDGET_EXCEEDED  - the reserve and the fixed rails no longer fit the source's limit
 *   SOURCE_CHANGED   - the board's power source changed (sys_power_source_e)
 *   PD_FIXED         - a VOLTAGE asked of the USB-PD charger was answered by a lower fixed profile (information)
 *   LIMIT_REACHED    - a rail write exceeded the power limit and was clamped (or refused if below min_mA)
 */
#define PWRM_EVENTS(X, P)                                                                                          \
  X(P, INPUT_OVP,        SYS_POWER_KIND_OVP,              SYS_EVENT_NOTIFY, { int32_t mV; })                       \
  X(P, BATTERY_LOW,      SYS_POWER_KIND_BATTERY_LOW,      SYS_EVENT_NOTIFY, { uint8_t pct; })                      \
  X(P, BATTERY_CRITICAL, SYS_POWER_KIND_BATTERY_CRITICAL, SYS_EVENT_NOTIFY, { uint8_t pct; })                      \
  X(P, BUDGET_EXCEEDED,  SYS_POWER_KIND_BUDGET_EXCEEDED,  SYS_EVENT_NOTIFY, { int32_t limit_mW; })                 \
  X(P, SOURCE_CHANGED,   SYS_POWER_KIND_SOURCE_CHANGED,   SYS_EVENT_NOTIFY, { uint8_t source; })                   \
  X(P, PD_FIXED,         0x10,                            SYS_EVENT_NOTIFY, { int32_t mV; })                       \
  X(P, LIMIT_REACHED,    0x11,                            SYS_EVENT_NOTIFY, { uint8_t device; uint32_t requested_mW; uint32_t limit_mW; })

SYS_EVENTS_DECLARE(PWRM, MODULE_ID_PWRM, SYS)

#ifdef __cplusplus
}
#endif
