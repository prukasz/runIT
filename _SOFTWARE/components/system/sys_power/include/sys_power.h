#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "status.h"

uint32_t sys_power_get_limit_mv(void);
uint32_t sys_power_get_limit_ma(void);
uint32_t sys_power_get_budget_mw(void);

#define SYS_POWER_LIMIT_MV sys_power_get_limit_mv()
#define SYS_POWER_LIMIT_MA sys_power_get_limit_ma()
#define SYS_POWER_BUDGET_MW sys_power_get_budget_mw()

status_rep_t sys_power_set_limits(uint32_t max_mv, uint32_t max_ma, uint32_t max_mw);

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
  status_rep_t (*set_enable)(void* device_handle, bool state);
  status_rep_t (*set_voltage)(void* device_handle, uint32_t voltage_mV);
  status_rep_t (*set_current)(void* device_handle, uint32_t current_mA);
  status_rep_t (*add_callback)(void* device_handle, sys_power_events_e on_event, void (*callback)(uint8_t device_id, sys_power_events_e triggered_by));
} sys_power_vreg_contract;

typedef struct sys_power_monitor_contract {
  status_rep_t (*get_voltage)(void* device_handle, uint8_t channel, int32_t* out_mV);
  status_rep_t (*get_current)(void* device_handle, uint8_t channel, int32_t* out_mA);
  status_rep_t (*add_callback)(void* device_handle, uint8_t channel, int32_t trigger_value, sys_power_events_e on_event, void (*callback)(uint8_t device_id, sys_power_events_e triggered_by));
} sys_power_monitor_contract;

typedef struct sys_power_usb_pd_contract {
  status_rep_t (*set_settings)(void* device_handle, uint32_t voltage_mV, uint32_t current_mA);
  status_rep_t (*list_options)(void* device_handle);
  status_rep_t (*get_limits)(void* device_handle, uint32_t* out_mV, uint32_t* out_mA);
} sys_power_usb_pd_contract;

/* ========================================================================== *
 * API SYSTEMOWE (APLIKACYJNE)
 * ========================================================================== */

// --- Rejestracja (Używane przez Adaptery) ---
status_rep_t sys_power_register_vreg(uint8_t device_id, void* handle, const sys_power_vreg_contract* contract);
status_rep_t sys_power_register_monitor(uint8_t device_id, void* handle, const sys_power_monitor_contract* contract);
status_rep_t sys_power_register_usb_pd(uint8_t device_id, void* handle, const sys_power_usb_pd_contract* contract);

status_rep_t sys_power_unregister(uint8_t device_id);
status_rep_t sys_power_budget_update_source(uint32_t max_mV, uint32_t max_mA);

// --- VREG API ---
status_rep_t sys_vreg_set_enable(uint8_t device_id, bool state);
status_rep_t sys_vreg_set_voltage(uint8_t device_id, uint32_t voltage_mV);
status_rep_t sys_vreg_set_current(uint8_t device_id, uint32_t current_mA);
status_rep_t sys_vreg_add_callback(uint8_t device_id, sys_power_events_e on_event, void (*callback)(uint8_t device_id, sys_power_events_e triggered_by));

// --- Monitor API ---
status_rep_t sys_power_monitor_get_voltage(uint8_t device_id, uint8_t channel, int32_t* out_mV);
status_rep_t sys_power_monitor_get_current(uint8_t device_id, uint8_t channel, int32_t* out_mA);
status_rep_t sys_power_monitor_add_callback(uint8_t device_id, uint8_t channel, int32_t trigger_value, sys_power_events_e on_event, void (*callback)(uint8_t device_id, sys_power_events_e triggered_by));

// --- USB PD API ---
status_rep_t sys_power_usb_pd_set(uint8_t device_id, uint32_t voltage_mV, uint32_t current_mA);
status_rep_t sys_power_usb_pd_list(uint8_t device_id);
status_rep_t sys_power_usb_pd_get_limits(uint8_t device_id, uint32_t* out_mV, uint32_t* out_mA);
