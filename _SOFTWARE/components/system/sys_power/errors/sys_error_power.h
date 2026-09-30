#pragma once
#include <stdint.h>
#include <stdio.h>

#define SYS_POWER_OWNER_MAP(X)                                                              \
  X(OWNER_SYS_POWER_BASE, 0xA400, "OWNER_SYS_POWER_BASE")                                 \
  X(OWNER_SYS_POWER_VREG_SET_ENABLE, 0xA406, "OWNER_SYS_POWER_VREG_SET_ENABLE")           \
  X(OWNER_SYS_POWER_VREG_SET_VOLTAGE, 0xA407, "OWNER_SYS_POWER_VREG_SET_VOLTAGE")         \
  X(OWNER_SYS_POWER_VREG_SET_CURRENT, 0xA408, "OWNER_SYS_POWER_VREG_SET_CURRENT")         \
  X(OWNER_SYS_POWER_MONITOR_GET_VOLTAGE, 0xA40A, "OWNER_SYS_POWER_MONITOR_GET_VOLTAGE")   \
  X(OWNER_SYS_POWER_MONITOR_GET_CURRENT, 0xA40B, "OWNER_SYS_POWER_MONITOR_GET_CURRENT")   \
  X(OWNER_SYS_POWER_MONITOR_SET_ALERT, 0xA40C, "OWNER_SYS_POWER_MONITOR_SET_ALERT")       \
  X(OWNER_SYS_POWER_USB_PD_SET, 0xA40D, "OWNER_SYS_POWER_USB_PD_SET")                     \
  X(OWNER_SYS_POWER_USB_PD_LIST, 0xA40E, "OWNER_SYS_POWER_USB_PD_LIST")                   \
  X(OWNER_SYS_POWER_USB_PD_GET_LIMITS, 0xA40F, "OWNER_SYS_POWER_USB_PD_GET_LIMITS")       \
  X(OWNER_SYS_POWER_MANAGER, 0xA410, "OWNER_SYS_POWER_MANAGER")                           \
  X(OWNER_SYS_POWER_SOURCE, 0xA411, "OWNER_SYS_POWER_SOURCE")                             \
  X(OWNER_SYS_POWER_BUDGET, 0xA412, "OWNER_SYS_POWER_BUDGET")                             \
  X(OWNER_SYS_POWER_EVENTS, 0xA413, "OWNER_SYS_POWER_EVENTS")                             \
  X(OWNER_SYS_POWER_SETTINGS, 0xA414, "OWNER_SYS_POWER_SETTINGS")

#define SYS_ERROR_POWER_MAP(X) \
  X(ERR_POWER_BUDGET_EXCEEDED, 0xA401, SE_LEVEL_HIGH, struct { uint8_t dev_id; /*@id device*/ uint32_t requested_mW; uint32_t available_mW; }) \
  X(ERR_POWER_SOURCE_BELOW_ALLOCATION, 0xA402, SE_LEVEL_HIGH, struct { uint32_t allocated_mW; uint32_t budget_mW; }) \
  X(ERR_POWER_FAULT, 0xA403, SE_LEVEL_MEDIUM, struct { uint8_t source_id; /*@id device*/ uint8_t channel; uint8_t event; /*@enum-ref sys_power_events_e*/ uint8_t response; /*@enum-ref sys_power_response_e*/ int32_t value; })

/** @brief Human-readable descriptions for the sys_power tags - see SE_describe_payload() in sys_error.h. */
#define SYS_ERROR_POWER_LOGGER_MAP(X) X(ERR_POWER_BUDGET_EXCEEDED) X(ERR_POWER_SOURCE_BELOW_ALLOCATION) X(ERR_POWER_FAULT)

#define LOG_BODY_ERR_POWER_BUDGET_EXCEEDED(p, out, out_size) snprintf((out), (out_size), "device %u would exceed the power budget: requested %lu mW, available %lu mW", (p)->dev_id, (unsigned long)(p)->requested_mW, (unsigned long)(p)->available_mW)
/* event: sys_power_events_e (1 OVP, 2 UVP, 3 short, 4 OCP warning, 5 OCP critical, 6 OTP);
   response: sys_power_response_e (0 notify, 1 disable, 2 reset, 3 safe state). */
#define LOG_BODY_ERR_POWER_FAULT(p, out, out_size)                                                                            \
  snprintf((out), (out_size), "power fault: event %u on device %u channel %u, value %ld, response %u", (p)->event, (p)->source_id, \
           (p)->channel, (long)(p)->value, (p)->response)
#define LOG_BODY_ERR_POWER_SOURCE_BELOW_ALLOCATION(p, out, out_size) snprintf((out), (out_size), "power source budget %lu mW is below the %lu mW already allocated", (unsigned long)(p)->budget_mW, (unsigned long)(p)->allocated_mW)
