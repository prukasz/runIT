#pragma once

#include "errors.h"
#include "sys_io_contract.h"
#include "sys_power_contract.h"
#include "sys_power_manager_errors.h"
#include "sys_power_manager_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Optional runtime power manager (SYS_POWER_MANAGER.MD).
 * Auto-starts (dynamic task) on the first LAYOUT write and stops cleanly on STOP. Tracks runtime loads (RAIL /
 * RAIL_REMOVE / SYS_DEVICE_EV_REMOVED), detects the active source, negotiates USB-PD, estimates LiPo charge, clamps
 * POWER_VREG writes to the power limit (emitting PWRM_EV_LIMIT_REACHED), and runs the fault response matrix.
 */

/* ----------------------------------------------------------------------------
 * Enums
 * ------------------------------------------------------------------------- */

//#ref-enum @alias Power Source
typedef enum {
  SYS_POWER_SOURCE_UNKNOWN = 0,  //@alias Unknown @description Source unknown
  SYS_POWER_SOURCE_USB = 1,      //@alias Usb @description USB source
  SYS_POWER_SOURCE_PSU = 2,      //@alias Power Supply @description External power supply
  SYS_POWER_SOURCE_BATTERY = 3,  //@alias Battery @description Battery source
} sys_power_source_e;

//#ref-enum @alias Power Budget Mode
typedef enum {
  SYS_POWER_BUDGET_DISABLED = 0,  //@alias Disable all limits @description Measure only; no budget limits enforced
  SYS_POWER_BUDGET_STRICT = 1,    //@alias Limit to total system power @description Hold device draw strictly within total system power
  SYS_POWER_BUDGET_MARGIN = 2,    //@alias Limit to total system power + margin @description Hold device draw within total system power plus soft margin
} sys_power_budget_mode_e;

//#ref-enum @alias Power Event Response
typedef enum {
  SYS_POWER_RESPONSE_NOTIFY = 0,      //@alias Notify @description Raise event only
  SYS_POWER_RESPONSE_DISABLE = 1,     //@alias Disable @description Suspend affected output
  SYS_POWER_RESPONSE_RESET = 2,       //@alias Reset @description Reset affected device
  SYS_POWER_RESPONSE_SAFE_STATE = 3,  //@alias Safe State @description Stop program and enter safe state
} sys_power_response_e;

//#ref-enum @alias Battery Chemistry
typedef enum {
  SYS_POWER_BATTERY_NONE = 0,  //@alias None @description No battery configured
  SYS_POWER_BATTERY_LIPO = 1,  //@alias LiPo @description Lithium polymer, 4.2V/cell
} sys_power_battery_e;

//#ref-enum @alias USB-PD Request
typedef enum {
  SYS_POWER_PD_OFF = 0,      //@alias Off @description Nothing asked: the charger's default 5 V
  SYS_POWER_PD_MAX = 1,      //@alias Highest offer @description The highest voltage offered, at the most current there
  SYS_POWER_PD_VOLTAGE = 2,  //@alias Set voltage @description The set voltage, at the most current offered there
} sys_power_pd_mode_e;

/* ----------------------------------------------------------------------------
 * Wire structs
 * ------------------------------------------------------------------------- */

#define SYS_POWER_TARGET_BOARD SYS_DEVICE_ID_NONE

/** @brief LAYOUT: board limits, input monitor, USB-PD sink, and eFuse source pins (SYS_DEVICE_ID_NONE / SYS_IO_PIN_NONE = none). */
typedef struct __attribute__((packed)) {
  uint32_t max_mV;
  uint32_t max_mA;
  uint32_t reserve_mW;
  uint32_t unknown_source_mA;
  uint8_t vreg_efficiency_pct;  // 1..100
  uint8_t input_device;
  uint8_t input_channel;
  uint8_t usb_pd;
  sys_io_pin_t usb_ok;
  sys_io_pin_t ext_ok;
} sys_power_layout_t;

/** @brief RAIL: a regulator load added under the budget at runtime. */
typedef struct __attribute__((packed)) {
  uint8_t vreg;
  uint8_t monitor_device;
  uint8_t monitor_channel;
  uint32_t min_mA;
} sys_power_rail_t;

/** @brief RAIL_REMOVE: removes a regulator load at runtime. */
typedef struct __attribute__((packed)) {
  uint8_t vreg;
} sys_power_rail_ref_t;

/** @brief CONFIG: budget mode, soft margin %, and fallback source (when no source pins). */
typedef struct __attribute__((packed)) {
  uint8_t mode;             /**< sys_power_budget_mode_e */
  uint8_t soft_margin_pct;  /**< Margin above budget in MARGIN mode (0..100%) */
  uint8_t source;           /**< sys_power_source_e when no source pins */
} sys_power_manager_cfg_t;

/** @brief PD: what to ask the USB-PD charger for. */
typedef struct __attribute__((packed)) {
  uint8_t mode;         /**< sys_power_pd_mode_e */
  uint32_t voltage_mV;  /**< Requested voltage in SYS_POWER_PD_VOLTAGE mode */
} sys_power_pd_t;

/** @brief PSU: external power supply limits (0 / 0 removes it). */
typedef struct __attribute__((packed)) {
  uint32_t max_mV;
  uint32_t max_mA;
} sys_power_psu_t;

/** @brief BATTERY: battery chemistry, series cell count, and alert thresholds. */
typedef struct __attribute__((packed)) {
  uint8_t chemistry;     /**< sys_power_battery_e */
  uint8_t cells;         /**< Series cell count */
  uint8_t low_pct;       /**< Low threshold % */
  uint8_t critical_pct;  /**< Critical threshold % */
} sys_power_battery_t;

/** @brief MATRIX row: target (SYS_POWER_TARGET_BOARD or rail ID) + response per sys_power_kind_e. */
typedef struct __attribute__((packed)) {
  uint8_t target;
  uint8_t responses[SYS_POWER_KIND_COUNT];
} sys_power_response_row_t;

/** @brief STATUS answer (40 B). */
typedef struct __attribute__((packed)) {
  uint8_t source;
  int32_t input_mV;
  int32_t input_mA;
  uint32_t source_mV;
  uint32_t source_mA;
  uint32_t budget_mW;
  uint32_t limit_mW;
  uint32_t allocated_mW;
  uint8_t battery_pct;  /**< 0xFF = unknown */
  uint8_t flags;        /**< SYS_POWER_FLAG_* */
  uint32_t pd_mV;
  uint32_t pd_mA;
  uint8_t pd_type;      /**< sys_power_usb_pd_option_type_e or SYS_POWER_PD_TYPE_NONE */
} sys_power_status_t;

#define SYS_POWER_FLAG_ENABLED     0x01u
#define SYS_POWER_FLAG_STRICT      0x02u
#define SYS_POWER_FLAG_OVER_BUDGET 0x04u
#define SYS_POWER_FLAG_INPUT_SEEN  0x08u

#define SYS_POWER_PCT_UNKNOWN  0xFFu
#define SYS_POWER_PD_TYPE_NONE 0xFFu

/* ----------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/* Lazy: stopped until the first LAYOUT (sys_power_manager_set_layout) starts it (its task, the POWER_VREG interceptor,
 * its listeners); stop frees it. While it is stopped every other call is ERR_PWRM_NOT_READY. */
MUST_USE err_h sys_power_manager_stop(void);
MUST_USE err_h sys_power_manager_set_layout(const sys_power_layout_t* layout);
MUST_USE err_h sys_power_manager_add_rail(const sys_power_rail_t* rail);
MUST_USE err_h sys_power_manager_remove_rail(const sys_power_rail_ref_t* ref);
MUST_USE err_h sys_power_manager_set_config(const sys_power_manager_cfg_t* cfg);
MUST_USE err_h sys_power_manager_get_config(sys_power_manager_cfg_t* out);
MUST_USE err_h sys_power_manager_set_psu(const sys_power_psu_t* psu);
MUST_USE err_h sys_power_manager_get_psu(sys_power_psu_t* out);
MUST_USE err_h sys_power_manager_set_battery(const sys_power_battery_t* battery);
MUST_USE err_h sys_power_manager_get_battery(sys_power_battery_t* out);
MUST_USE err_h sys_power_manager_set_pd(const sys_power_pd_t* pd);
MUST_USE err_h sys_power_manager_get_pd(sys_power_pd_t* out);
MUST_USE err_h sys_power_manager_set_matrix(const sys_power_response_row_t* rows, uint8_t count);
MUST_USE err_h sys_power_manager_get_matrix(sys_power_response_row_t* rows, uint8_t cap, uint8_t* count);
MUST_USE err_h sys_power_manager_get_status(sys_power_status_t* out);

#ifdef __cplusplus
}
#endif
