#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sys_error.h"

#include "sys_callbacks.h"

uint32_t sys_power_get_limit_mv(void);
uint32_t sys_power_get_limit_ma(void);
uint32_t sys_power_get_budget_mw(void);

#define SYS_POWER_LIMIT_MV sys_power_get_limit_mv()
#define SYS_POWER_LIMIT_MA sys_power_get_limit_ma()
#define SYS_POWER_BUDGET_MW sys_power_get_budget_mw()

#define SYS_PWR_CB(_ctx, _chan, _event, _value, _route_mask, _action_mask)    \
  do {                                                                        \
    cb_event_t __cb_evt;                                                      \
    memset(&__cb_evt, 0, sizeof(__cb_evt));                                   \
    __cb_evt.head.callback_type = CALLBACK_PWR;                               \
    __cb_evt.head.route_mask = (_route_mask);                                 \
    __cb_evt.head.action_id = (_action_mask);                                 \
    __cb_evt.event.pwr.device_id = (_ctx)->base.device_id;                    \
    __cb_evt.event.pwr.channel_id = (_chan);                                  \
    __cb_evt.event.pwr.trigger_event = (_event);                              \
    __cb_evt.event.pwr.trigger_value = (_value);                              \
    sys_callback_trigger(&__cb_evt);                                          \
  } while (0)

err_h sys_power_set_limits(uint32_t max_mv, uint32_t max_ma, uint32_t max_mw);

typedef enum sys_power_events_e {
  SYS_PWR_EVENT_NONE = 0,
  SYS_PWR_EVENT_OVP = 1,
  SYS_PWR_EVENT_UVP = 2,
  SYS_PWR_EVENT_SPC = 3,
  SYS_PWR_EVENT_OCP_WARNING = 4,
  SYS_PWR_EVENT_OCP_CRITICAL = 5,
  SYS_PWR_EVENT_OTP = 6,
} sys_power_events_e;

/* ========================================================================== *
 * KONTRAKTY DOMENOWE (VTABLES)
 * ========================================================================== */

typedef struct sys_power_vreg_contract {
  err_h (*set_enable)(void* device_handle, bool state);
  err_h (*set_voltage)(void* device_handle, uint32_t voltage_mV);
  err_h (*set_current)(void* device_handle, uint32_t current_mA);
  err_h (*add_callback)(void* device_handle, sys_power_events_e on_event, uint16_t route_mask, uint64_t action_mask);
} sys_power_vreg_contract;

typedef struct sys_power_monitor_contract {
  err_h (*get_voltage)(void* device_handle, uint8_t channel, int32_t* out_mV);
  err_h (*get_current)(void* device_handle, uint8_t channel, int32_t* out_mA);
  err_h (*add_callback)(void* device_handle, uint8_t channel, int32_t trigger_value, sys_power_events_e on_event, uint16_t route_mask, uint64_t action_mask);
} sys_power_monitor_contract;

typedef struct sys_power_usb_pd_contract {
  err_h (*set_settings)(void* device_handle, uint32_t voltage_mV, uint32_t current_mA);
  err_h (*list_options)(void* device_handle);
  err_h (*get_limits)(void* device_handle, uint32_t* out_mV, uint32_t* out_mA);
} sys_power_usb_pd_contract;

/* ========================================================================== *
 * API SYSTEMOWE (APLIKACYJNE)
 * ========================================================================== */

err_h sys_power_budget_update_source(uint32_t max_mV, uint32_t max_mA);

// --- VREG API ---
err_h sys_vreg_set_enable(uint8_t device_id, bool state);
err_h sys_vreg_set_voltage(uint8_t device_id, uint32_t voltage_mV);
err_h sys_vreg_set_current(uint8_t device_id, uint32_t current_mA);
err_h sys_vreg_add_callback(uint8_t device_id, sys_power_events_e on_event, uint16_t route_mask, uint64_t action_mask);

// --- Monitor API ---
err_h sys_power_monitor_get_voltage(uint8_t device_id, uint8_t channel, int32_t* out_mV);
err_h sys_power_monitor_get_current(uint8_t device_id, uint8_t channel, int32_t* out_mA);
err_h sys_power_monitor_add_callback(uint8_t device_id, uint8_t channel, int32_t trigger_value, sys_power_events_e on_event, uint16_t route_mask, uint64_t action_mask);

// --- USB PD API ---
err_h sys_power_usb_pd_set(uint8_t device_id, uint32_t voltage_mV, uint32_t current_mA);
err_h sys_power_usb_pd_list(uint8_t device_id);
err_h sys_power_usb_pd_get_limits(uint8_t device_id, uint32_t* out_mV, uint32_t* out_mA);
