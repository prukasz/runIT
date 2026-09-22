#pragma once
#include "sys_error.h"
#include "sys_event.h"
#include "sys_io.h"

//#ref-enum @alias Power Event
typedef enum sys_power_events_e {
  SYS_PWR_EVENT_NONE = 0, //@alias None @description Nothing wrong - normal operation
  SYS_PWR_EVENT_OVP = 1, //@alias Over-Voltage @description The voltage went above the safe limit
  SYS_PWR_EVENT_UVP = 2, //@alias Under-Voltage @description The voltage dropped below the safe limit
  SYS_PWR_EVENT_SPC = 3, //@alias Short Circuit @description A short circuit was detected on the output
  SYS_PWR_EVENT_OCP_WARNING = 4, //@alias Over-Current Warning @description Current draw is approaching the limit - not cut off yet, just a heads-up
  SYS_PWR_EVENT_OCP_CRITICAL = 5, //@alias Over-Current Critical @description Current draw exceeded the limit - output is protected/shut down
  SYS_PWR_EVENT_OTP = 6, //@alias Over-Temperature @description The device got too hot and triggered thermal protection
  SYS_PWR_EVENT_BATTERY_LOW = 7, //@alias Battery Low @description The battery charge fell to the low threshold
  SYS_PWR_EVENT_BATTERY_CRITICAL = 8, //@alias Battery Critical @description The battery charge fell to the critical threshold
  SYS_PWR_EVENT_BUDGET_EXCEEDED = 9, //@alias Budget Exceeded @description The power source can no longer cover what the outputs are set to draw
  SYS_PWR_EVENT_SOURCE_CHANGED = 10, //@alias Source Changed @description The board switched to another power source (the value is the new source)
} sys_power_events_e;

/* Number of sys_power_events_e values (response matrix size). */
#define SYS_PWR_EVENT_COUNT 11

/**
 * @brief Publish a power event (SYS_EVENT_DOMAIN_POWER) from a power device
 * adapter or the manager.
 * @param hops SYS_EVENT_CAUSED_BY(cause) when raised from a listener, else 0.
 */
static inline SE_MUST_USE err_h sys_power_publish(uint8_t device_id, uint8_t channel, sys_power_events_e event, int32_t value, uint8_t hops) {
  sys_event_t ev = {.domain = SYS_EVENT_DOMAIN_POWER, .device_id = device_id, .channel = channel, .event = (uint8_t)event, .value = value, .hops = hops};
  return sys_event_publish(&ev);
}

/* ========================================================================== *
 * KONTRAKTY DOMENOWE (VTABLES)
 * ========================================================================== */

typedef struct sys_power_vreg_contract_t {
  err_h (*set_enable)(void* device_handle, bool state);
  err_h (*set_voltage)(void* device_handle, uint32_t voltage_mV);
  err_h (*set_current)(void* device_handle, uint32_t current_mA);
} sys_power_vreg_contract_t;

typedef struct sys_power_monitor_contract_t {
  err_h (*get_voltage)(void* device_handle, uint8_t channel, int32_t* out_mV);
  err_h (*get_current)(void* device_handle, uint8_t channel, int32_t* out_mA);
  /* Hardware current alert: alert = SYS_PWR_EVENT_OCP_CRITICAL or _OCP_WARNING. */
  err_h (*set_alert)(void* device_handle, uint8_t channel, sys_power_events_e alert, int32_t threshold_mA);
} sys_power_monitor_contract_t;

/* USB PD: a source offers at most 7 SPR + 6 EPR power data objects. */
#define SYS_POWER_USB_PD_OPTIONS_MAX 13

//#ref-enum @alias USB-PD Option Type
typedef enum sys_power_usb_pd_option_type_e {
  SYS_POWER_USB_PD_OPTION_FIXED = 0, //@alias Fixed @description A fixed voltage.
  SYS_POWER_USB_PD_OPTION_PPS = 1,   //@alias PPS @description Programmable voltage within a range (standard power range, up to 21 V).
  SYS_POWER_USB_PD_OPTION_AVS = 2,   //@alias AVS @description Adjustable voltage within a range (extended power range, up to 28 V).
} sys_power_usb_pd_option_type_e;

/* One power option offered by the USB-PD source. */
typedef struct sys_power_usb_pd_option_t {
  uint8_t slot;    /* source PDO position, 1-based */
  uint8_t type;    /* sys_power_usb_pd_option_type_e */
  uint16_t min_mV; /* equal to max_mV for a fixed option */
  uint16_t max_mV;
  uint16_t max_mA;
} sys_power_usb_pd_option_t;

typedef struct sys_power_usb_pd_contract_t {
  err_h (*set_settings)(void* device_handle, uint32_t voltage_mV, uint32_t current_mA);
  err_h (*list_options)(void* device_handle, sys_power_usb_pd_option_t* out_options, uint8_t max_options, uint8_t* out_count);
  err_h (*get_limits)(void* device_handle, int32_t* out_mV, int32_t* out_mA);
} sys_power_usb_pd_contract_t;

/* Member names of each power contract in order, NULL-terminated (feature id = index). */
extern const char* const sys_power_vreg_feature_names[];
extern const char* const sys_power_monitor_feature_names[];
extern const char* const sys_power_usb_pd_feature_names[];

/* ========================================================================== *
 * API SYSTEMOWE (APLIKACYJNE)
 * ========================================================================== */

/* ---- Power manager: one board input feeding always-on loads and the vreg rails.
 * The board config describes the layout (sys_power_board_t), so nothing below
 * assumes a particular board revision. See SYS_POWER.MD "Power manager". ---- */

/* device_id meaning "no device" in the board description, and the source of a
   board-level event (sys_event_t.device_id; only SYS_EVENT_ANY matches it). */
#define SYS_POWER_NO_DEVICE 0xFF

//#ref-enum @alias Power Source
typedef enum sys_power_source_e {
  SYS_POWER_SOURCE_UNKNOWN = 0, //@alias Unknown @description Not identified; the budget uses the measured input voltage and the board's assumed current
  SYS_POWER_SOURCE_USB_PD = 1,  //@alias USB-C @description USB-C Power Delivery; the budget uses the input voltage and the source's current
  SYS_POWER_SOURCE_PSU = 2,     //@alias Power Supply @description External supply; the budget uses the input voltage and the current entered by the user
  SYS_POWER_SOURCE_BATTERY = 3, //@alias Battery @description Battery; the budget uses the measured voltage, current limited only by the board
} sys_power_source_e;

//#ref-enum @alias Power Event Response
typedef enum sys_power_response_e {
  SYS_POWER_RESPONSE_NOTIFY = 0,     //@alias Notify @description Only raise the event
  SYS_POWER_RESPONSE_DISABLE = 1,    //@alias Disable @description Suspend the affected output: that rail, or every rail for a board-level event
  SYS_POWER_RESPONSE_RESET = 2,      //@alias Reset @description Reset the affected device: that rail, or every rail for a board-level event
  SYS_POWER_RESPONSE_SAFE_STATE = 3, //@alias Safe State @description Stop the program and suspend every device
} sys_power_response_e;

//#ref-enum @alias Battery Chemistry
typedef enum sys_power_battery_e {
  SYS_POWER_BATTERY_NONE = 0, //@alias None @description No battery configured
  SYS_POWER_BATTERY_LIPO = 1, //@alias LiPo @description Lithium polymer, 4.2 V per cell when full
} sys_power_battery_e;

/* One monitor channel: a device with the POWER_MONITOR contract and its channel. */
typedef struct sys_power_channel_t {
  uint8_t device_id; /* SYS_POWER_NO_DEVICE = not present */
  uint8_t channel;
} sys_power_channel_t;

/* A pin that identifies the active source when it reads active_level. */
typedef struct sys_power_source_pin_t {
  sys_io_pin_ref_t pin; /* its mode is applied by the power manager */
  bool active_level;
  sys_power_source_e source;
} sys_power_source_pin_t;

/* One output rail fed from the board input and covered by the budget. */
typedef struct sys_power_consumer_t {
  uint8_t vreg_id;             /* device with the POWER_VREG contract */
  sys_power_channel_t monitor; /* channel measuring this rail's output, or device SYS_POWER_NO_DEVICE */
} sys_power_consumer_t;

/* Board power layout. Owned by the board config; must stay valid (static). */
typedef struct sys_power_board_t {
  uint32_t max_mV;             /* hardware trip: highest input voltage (budget cap; above it OVP is raised) */
  uint32_t max_mA;             /* hardware trip: highest input current (budget cap) */
  uint32_t reserve_mW;         /* input power always taken by fixed loads (for example the 3.3 V buck) */
  uint32_t unknown_source_mA;  /* current assumed while the source's limit is unknown */
  uint8_t vreg_efficiency_pct; /* rail input power = output power * 100 / efficiency */
  sys_power_channel_t input;   /* channel measuring the board input */
  uint8_t usb_pd_id;           /* USB-PD sink device, or SYS_POWER_NO_DEVICE */
  const sys_power_source_pin_t* source_pins; /* first match wins; NULL when the board has none */
  uint8_t source_pin_count;
  const sys_power_consumer_t* consumers;
  uint8_t consumer_count;      /* at most CONFIG_SYS_POWER_MAX_CONSUMERS */
  const uint8_t* responses;    /* default sys_power_response_e per event, SYS_PWR_EVENT_COUNT entries (NULL = all Notify) */
} sys_power_board_t;

/* Snapshot of the manager's state (sys_power_get_status). */
typedef struct sys_power_status_t {
  uint8_t source;        /* sys_power_source_e */
  int32_t input_mV;      /* measured board input */
  int32_t input_mA;
  uint32_t source_mV;    /* source limits used for the budget (after the hardware caps) */
  uint32_t source_mA;
  uint32_t budget_mW;
  uint32_t allocated_mW; /* reserve + rails */
  uint8_t battery_pct;   /* 0-100, or 0xFF when unknown / no battery */
} sys_power_status_t;

/* Board-level safe state (for example VM stop + suspend all), registered by the application. */
typedef err_h (*sys_power_safe_state_f)(void);

/**
 * @brief Start the power manager for @p board: load the persisted settings
 * (PSU, battery) and start the manager task. Needs sys_settings_init().
 */
SE_MUST_USE err_h sys_power_init(const sys_power_board_t* board);

/** @brief Response used for SYS_POWER_RESPONSE_SAFE_STATE; default is sys_device_suspend_all(). */
void sys_power_register_safe_state(sys_power_safe_state_f fn);

/** @brief External supply limits, persisted. 0 / 0 clears them. */
SE_MUST_USE err_h sys_power_set_psu(uint32_t max_mV, uint32_t max_mA);

/**
 * @brief Battery setup, persisted. SYS_POWER_BATTERY_NONE clears it.
 * Thresholds in percent: 0 < critical_pct < low_pct <= 100.
 */
SE_MUST_USE err_h sys_power_set_battery(sys_power_battery_e chemistry, uint8_t cells, uint8_t low_pct, uint8_t critical_pct);

/** @brief Change the response to one event at runtime (not persisted). */
SE_MUST_USE err_h sys_power_set_response(sys_power_events_e event, sys_power_response_e response);

SE_MUST_USE err_h sys_power_get_status(sys_power_status_t* out);

/** @brief Hardware input caps of the board (0 before sys_power_init()). */
uint32_t sys_power_get_max_mV(void);
uint32_t sys_power_get_max_mA(void);

// --- VREG API ---
SE_MUST_USE err_h sys_power_vreg_set_enable(uint8_t device_id, bool state);
SE_MUST_USE err_h sys_power_vreg_set_voltage(uint8_t device_id, uint32_t voltage_mV);
SE_MUST_USE err_h sys_power_vreg_set_current(uint8_t device_id, uint32_t current_mA);

// --- Monitor API ---
SE_MUST_USE err_h sys_power_monitor_get_voltage(uint8_t device_id, uint8_t channel, int32_t* out_mV);
SE_MUST_USE err_h sys_power_monitor_get_current(uint8_t device_id, uint8_t channel, int32_t* out_mA);
/** @brief Arm a hardware current alert; it is published as that event (SYS_EVENT_DOMAIN_POWER). */
SE_MUST_USE err_h sys_power_monitor_set_alert(uint8_t device_id, uint8_t channel, sys_power_events_e alert, int32_t threshold_mA);

// --- USB PD API ---
SE_MUST_USE err_h sys_power_usb_pd_set(uint8_t device_id, uint32_t voltage_mV, uint32_t current_mA);
SE_MUST_USE err_h sys_power_usb_pd_list(uint8_t device_id, sys_power_usb_pd_option_t* out_options, uint8_t max_options, uint8_t* out_count);
SE_MUST_USE err_h sys_power_usb_pd_get_limits(uint8_t device_id, int32_t* out_mV, int32_t* out_mA);
