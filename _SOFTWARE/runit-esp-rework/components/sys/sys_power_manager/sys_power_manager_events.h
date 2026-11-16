#pragma once

#include <stdint.h>

#include "sys_event.h"
#include "sys_power_manager_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Events of the power manager itself: module MODULE_ID_PWRM (0x22, declared in sys_power_manager_errors.h and claimed
 * in sys_power_manager.c), source SYS_DEVICE_ID_NONE, channel 0. The device events it
 * answers (VREG, PMON) are the devices' own. All matter to the user: NOTIFY.
 *   SOURCE_CHANGED   - the board's power source changed (sys_power_source_e)
 *   BATTERY_LOW      - the battery fell to its low threshold
 *   BATTERY_CRITICAL - the battery fell to its critical threshold
 *   BUDGET_EXCEEDED  - the reserve and the fixed rails no longer fit the source's limit
 *   INPUT_OVP        - the input voltage is above its limit
 *   PD_FIXED         - a VOLTAGE asked of the USB-PD charger was answered by a lower fixed profile (information)
 * @usage ERR_TRY(SYS_EVENT_PUBLISH(PWRM_EV_BATTERY_LOW, SYS_DEVICE_ID_NONE, 0, .pct = pct));
 */
#define PWRM_EVENTS(X, P)                                               \
  X(P, SOURCE_CHANGED,   0x01, SYS_EVENT_NOTIFY, { uint8_t source; })   \
  X(P, BATTERY_LOW,      0x02, SYS_EVENT_NOTIFY, { uint8_t pct; })      \
  X(P, BATTERY_CRITICAL, 0x03, SYS_EVENT_NOTIFY, { uint8_t pct; })      \
  X(P, BUDGET_EXCEEDED,  0x04, SYS_EVENT_NOTIFY, { int32_t limit_mW; }) \
  X(P, INPUT_OVP,        0x05, SYS_EVENT_NOTIFY, { int32_t mV; })       \
  X(P, PD_FIXED,         0x06, SYS_EVENT_NOTIFY, { int32_t mV; })

SYS_EVENTS_DECLARE(PWRM, SYS)

#ifdef __cplusplus
}
#endif
