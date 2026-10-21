#pragma once

#include "errors.h"
#include "sys_device.h"
#include "sys_event.h"
#include "sys_io.h"
#include "sys_power_manager_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Power manager (SYS_POWER_MANAGER.MD): one board input feeding always-on loads (the reserve) and the
 * regulator rails listed in the board description. It identifies the source and its budget, keeps the rails'
 * allocation under it (strictly, or with a margin), splits what's left between rails without a user current limit,
 * asks the USB-PD charger for the voltage set, estimates battery charge and answers power events with the response
 * matrix. The layout (rails, monitors, source pins) is the board's; the settings are remote. It sees every POWER_VREG op of a
 * budgeted rail (a sys_device guard), remote commands included.
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

/* What a power event is, for the response matrix: the wire event numbers (one entry each). A device event maps to
 * one: VREG OVP -> OVP, VREG OCP -> OCP_CRITICAL, VREG SCP -> SPC, monitor OCP warning / critical -> the same.
 * UVP and OTP have no source yet (kept for the wire). */
//#ref-enum @alias Power Event
typedef enum {
  SYS_POWER_KIND_NONE = 0,              //@alias None @description No event
  SYS_POWER_KIND_OVP = 1,               //@alias Over-voltage @description Overvoltage event
  SYS_POWER_KIND_UVP = 2,               //@alias Under-voltage @description Undervoltage event
  SYS_POWER_KIND_SPC = 3,               //@alias Short circuit @description Short circuit event
  SYS_POWER_KIND_OCP_WARNING = 4,       //@alias Over-current warning @description Overcurrent warning
  SYS_POWER_KIND_OCP_CRITICAL = 5,      //@alias Over-current critical @description Overcurrent critical
  SYS_POWER_KIND_OTP = 6,               //@alias Over-temperature @description Overtemperature event
  SYS_POWER_KIND_BATTERY_LOW = 7,       //@alias Battery low @description Battery low
  SYS_POWER_KIND_BATTERY_CRITICAL = 8,  //@alias Battery critical @description Battery critical
  SYS_POWER_KIND_BUDGET_EXCEEDED = 9,   //@alias Power budget exceeded @description Power budget exceeded
  SYS_POWER_KIND_SOURCE_CHANGED = 10,   //@alias Power source changed @description Power source changed
  SYS_POWER_KIND_COUNT,
} sys_power_kind_e;

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

/* What the manager asks the USB-PD charger for (SET_PD). */
//#ref-enum @alias USB-PD Request
typedef enum {
  SYS_POWER_PD_OFF = 0,      //@alias Off @description Nothing asked: the charger's default 5 V
  SYS_POWER_PD_MAX = 1,      //@alias Highest offer @description The highest voltage offered, at the most current there
  SYS_POWER_PD_VOLTAGE = 2,  //@alias Set voltage @description The set voltage, at the most current offered there
} sys_power_pd_mode_e;

/* ----------------------------------------------------------------------------
 * Events of the manager itself (sys_event.h "Events tables"): contract 0x06, source SYS_DEVICE_ID_NONE, channel 0.
 * The device events it answers (VREG, monitor) are the devices' own.
 * ------------------------------------------------------------------------- */

#define PWRM_EVENTS(X, P)                        \
  X(P, SOURCE_CHANGED,   0x00, SYS_EVENT_NOTIFY) \
  X(P, BATTERY_LOW,      0x01, SYS_EVENT_NOTIFY) \
  X(P, BATTERY_CRITICAL, 0x02, SYS_EVENT_NOTIFY) \
  X(P, BUDGET_EXCEEDED,  0x03, SYS_EVENT_NOTIFY) \
  X(P, INPUT_OVP,        0x04, SYS_EVENT_NOTIFY) \
  X(P, PD_FIXED,         0x05, SYS_EVENT_NOTIFY)

enum { PWRM_CONTRACT = SYS_DEVICE_CONTRACT_POWER_MANAGER };  // events only: the manager has no ops
SYS_EVENTS_DECLARE(PWRM)

/* ----------------------------------------------------------------------------
 * Board description (static, from the board config)
 * ------------------------------------------------------------------------- */

/** @brief One monitor channel: a device with the POWER_MONITOR contract, or device SYS_DEVICE_ID_NONE. */
typedef struct {
  uint16_t device;
  uint8_t channel;
} sys_power_channel_t;

/** @brief A regulator rail fed from the board input, under the budget. */
typedef struct {
  uint16_t vreg;                // device with the POWER_VREG contract
  sys_power_channel_t monitor;  // measures this rail's output (its alerts get the rail's response)
  uint32_t min_mA;              // the regulator's lowest current limit: a share below it can't run the rail
  const uint8_t* responses;     // this rail's default responses (SYS_POWER_KIND_COUNT); NULL = the board's
} sys_power_consumer_t;

/** @brief The eFuse power-good outputs of the board's two inputs (read as plain inputs, high = that input is live). */
typedef struct {
  sys_io_pin_t usb_ok;  // the USB-C input
  sys_io_pin_t ext_ok;  // the external input (a supply or a battery); it has priority when both are live
} sys_power_source_pins_t;

/** @brief The board's power layout. Must stay valid (static): the rails and monitors never change at run time. */
typedef struct {
  uint32_t max_mV;              // hardware trip of the input: budget cap; above it INPUT_OVP
  uint32_t max_mA;              // hardware trip of the input: budget cap
  uint32_t reserve_mW;          // input power always taken by fixed loads (the 3.3 V buck)
  uint32_t unknown_source_mA;   // current assumed while the source's limit is unknown (also USB)
  uint8_t vreg_efficiency_pct;  // rail input power = output power * 100 / efficiency
  sys_power_channel_t input;    // measures the board input
  uint16_t usb_pd;              // USB-PD sink device (POWER_USB_PD contract), or SYS_DEVICE_ID_NONE
  const sys_power_source_pins_t* source_pins;  // NULL = none: the source is what SET_CONFIG says
  const sys_power_consumer_t* consumers;
  uint8_t consumer_count;       // at most CONFIG_SYS_POWER_MANAGER_MAX_CONSUMERS
  const uint8_t* responses;     // default sys_power_response_e per sys_power_kind_e (SYS_POWER_KIND_COUNT; NULL = Notify):
                                // the board's events, and every rail without its own
} sys_power_board_t;

/* Response target: the board (its input, the manager's own events) or a rail by its device ID. */
#define SYS_POWER_TARGET_BOARD 0xFFFFu

/* ----------------------------------------------------------------------------
 * Settings (applied with SET, kept as records SYS_ACTIONS_ID_POWER_*; also the SET / GET payloads)
 * ------------------------------------------------------------------------- */

/** @brief How the budget works (SET_CONFIG / GET_CONFIG). */
typedef struct __attribute__((packed)) {
  uint8_t enabled;          /**< 1 = enabled, 0 = measure only */
  uint8_t strict;           /**< 1 = strict budget, 0 = soft margin */
  uint8_t soft_margin_pct;  /**< Soft margin above budget (0..100%) */
  uint8_t source;           /**< sys_power_source_e: the source on a board without source pins (ignored with them) */
} sys_power_manager_cfg_t;

/** @brief What to ask the USB-PD charger for (SET_PD / GET_PD). */
typedef struct __attribute__((packed)) {
  uint8_t mode;         /**< sys_power_pd_mode_e */
  uint32_t voltage_mV;  /**< The voltage of SYS_POWER_PD_VOLTAGE (else 0) */
} sys_power_pd_t;

/** @brief An external supply's limits (SET_PSU); 0 / 0 = none. */
typedef struct __attribute__((packed)) {
  uint32_t max_mV;  /**< Maximum voltage in mV */
  uint32_t max_mA;  /**< Maximum current in mA */
} sys_power_psu_t;

/** @brief The battery configuration (SET_BATTERY). */
typedef struct __attribute__((packed)) {
  uint8_t chemistry;     /**< sys_power_battery_e */
  uint8_t cells;         /**< Number of cells in series */
  uint8_t low_pct;       /**< Battery low threshold % */
  uint8_t critical_pct;  /**< Battery critical threshold % */
} sys_power_battery_t;

/** @brief One row of the response matrix with its target (MATRIX, both sides): the board (SYS_POWER_TARGET_BOARD) or a rail. */
typedef struct __attribute__((packed)) {
  uint16_t target;                            /**< SYS_POWER_TARGET_BOARD or the rail's device ID */
  uint8_t responses[SYS_POWER_KIND_COUNT];    /**< sys_power_response_e per sys_power_kind_e */
} sys_power_response_row_t;

/** @brief GET_STATUS answer. */
typedef struct __attribute__((packed)) {
  uint8_t source;         /**< sys_power_source_e */
  int32_t input_mV;       /**< Input voltage in mV */
  int32_t input_mA;       /**< Input current in mA */
  uint32_t source_mV;     /**< Source voltage limit in mV */
  uint32_t source_mA;     /**< Source current limit in mA */
  uint32_t budget_mW;     /**< Budget in mW without soft margin */
  uint32_t limit_mW;      /**< Rail limit in mW including soft margin */
  uint32_t allocated_mW;  /**< Allocated power in mW */
  uint8_t battery_pct;    /**< Battery charge % (0xFF = unknown) */
  uint8_t flags;          /**< SYS_POWER_FLAG_* bitmask */
  uint32_t pd_mV;         /**< USB-PD: the voltage of the profile in use (0 = none) */
  uint32_t pd_mA;         /**< USB-PD: the most current it offers */
  uint8_t pd_type;        /**< sys_power_usb_pd_option_type_e of that profile, SYS_POWER_PD_TYPE_NONE without one */
} sys_power_status_t;

#define SYS_POWER_FLAG_ENABLED     0x01u
#define SYS_POWER_FLAG_STRICT      0x02u
#define SYS_POWER_FLAG_OVER_BUDGET 0x04u
#define SYS_POWER_FLAG_INPUT_SEEN  0x08u  // the input monitor answered at least once

#define SYS_POWER_PCT_UNKNOWN   0xFFu
#define SYS_POWER_PD_TYPE_NONE  0xFFu

/* ----------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/** @brief The app's safe state (SAFE_STATE response). Without one: sys_device_sweep(SYS_DEVICE_OP_SUSPEND). */
typedef void (*sys_power_safe_state_f)(void);

/**
 * @brief Starts with the default settings, guards POWER_VREG, subscribes to the rails' and monitors' events and starts the task.
 * Boot step, after sys_event_init. The devices may come later: a missing input monitor is
 * skipped until it answers, a missing rail counts as off.
 */
MUST_USE err_h sys_power_manager_init(const sys_power_board_t* board, sys_power_safe_state_f safe_state);

/** @brief Stored at once, applied on the next tick (woken now). enabled, strict 0 / 1, soft_margin_pct 0..100, source a
 * sys_power_source_e. */
MUST_USE err_h sys_power_manager_set_config(const sys_power_manager_cfg_t* cfg);
void sys_power_manager_get_config(sys_power_manager_cfg_t* out);

/** @brief Stored. 0 / 0 removes it. */
MUST_USE err_h sys_power_manager_set_psu(const sys_power_psu_t* psu);
/** @brief Stored. chemistry NONE removes it. A battery set: a live external input is the battery, else a PSU. */
MUST_USE err_h sys_power_manager_set_battery(const sys_power_battery_t* battery);
/** @brief The external supply as set (0 / 0 = none). */
void sys_power_manager_get_psu(sys_power_psu_t* out);
/** @brief The battery as set (chemistry NONE = none). */
void sys_power_manager_get_battery(sys_power_battery_t* out);

/**
 * @brief Rows of the matrix, each with its target (what sys_power_manager_get_matrix gives): matched by target, so the
 * order doesn't matter; a target left out keeps its row. All checked before anything changes. The app saves the
 * MATRIX write as the record SYS_ACTIONS_ID_POWER_RESPONSES.
 * @return ERR_PWRM_NOT_RAIL for a target that is neither the board nor a rail, ERR_PWRM_BAD_ARG for a response out of
 * range, ERR_CMD_BAD_LEN for more rows than the board has.
 */
MUST_USE err_h sys_power_manager_set_matrix(const sys_power_response_row_t* rows, uint8_t count);

/**
 * @brief The whole matrix, each row with its target: the board first, then the rails in the board config's order (what a
 * MATRIX read answers). `cap` rows at most; `*count` gets the number written.
 * @return ERR_PWRM_NOT_READY before init, ERR_CMD_BAD_LEN when `cap` is too small.
 */
MUST_USE err_h sys_power_manager_get_matrix(sys_power_response_row_t* rows, uint8_t cap, uint8_t* count);

MUST_USE err_h sys_power_manager_get_status(sys_power_status_t* out);

/**
 * @brief What to ask the USB-PD charger for, on every USB source (asked again when the offer changes). MAX: the highest
 * voltage offered; VOLTAGE: `voltage_mV` (1 .. the board's max_mV). A range profile (PPS / AVS) gives the voltage
 * itself, else the highest fixed profile at or below it (PD_FIXED is published); the current is the most that profile
 * offers. Both capped by the board's max_mV / max_mA.
 */
MUST_USE err_h sys_power_manager_set_pd(const sys_power_pd_t* pd);
void sys_power_manager_get_pd(sys_power_pd_t* out);

#ifdef __cplusplus
}
#endif
