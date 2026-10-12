#pragma once

#include "errors.h"
#include "sys_device.h"
#include "sys_event.h"
#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Power manager (SYS_POWER_MANAGER.MD): one board input feeding always-on loads (the reserve) and the
 * regulator rails listed in the board description. It identifies the source and its budget, keeps the rails'
 * allocation under it (strictly, or with a margin), splits what's left between rails without a user current limit,
 * estimates battery charge and answers power events with the response matrix. It sees every POWER_VREG op of a
 * budgeted rail (a sys_device guard), remote commands included.
 */

/* ----------------------------------------------------------------------------
 * Enums
 * ------------------------------------------------------------------------- */

//#ref-enum @alias Power Source
typedef enum {
  SYS_POWER_SOURCE_UNKNOWN = 0,  //@alias Unknown @description Not identified; the budget uses the measured input voltage and the board's assumed current
  SYS_POWER_SOURCE_USB = 1,  //@alias Usb
  SYS_POWER_SOURCE_PSU = 2,  //@alias Power Supply @description External supply; the budget uses the input voltage and the current entered by the user
  SYS_POWER_SOURCE_BATTERY = 3,  //@alias Battery @description Battery; the budget uses the measured voltage, current limited only by the board
} sys_power_source_e;

/* What a power event is, for the response matrix: the wire event numbers (one entry each). A device event maps to
 * one: VREG OVP -> OVP, VREG OCP -> OCP_CRITICAL, VREG SCP -> SPC, monitor OCP warning / critical -> the same.
 * UVP and OTP have no source yet (kept for the wire). */
//#ref-enum @alias Power Event
typedef enum {
  SYS_POWER_KIND_NONE = 0,  //@alias None @description No event
  SYS_POWER_KIND_OVP = 1,  //@alias Over-voltage @description A rail's voltage went above its limit
  SYS_POWER_KIND_UVP = 2,  //@alias Under-voltage @description A rail's voltage fell below its limit
  SYS_POWER_KIND_SPC = 3,  //@alias Short circuit @description A rail is shorted
  SYS_POWER_KIND_OCP_WARNING = 4,  //@alias Over-current warning @description A rail's current passed the warning threshold
  SYS_POWER_KIND_OCP_CRITICAL = 5,  //@alias Over-current critical @description A rail's current passed the critical threshold
  SYS_POWER_KIND_OTP = 6,  //@alias Over-temperature @description A rail's regulator is too hot
  SYS_POWER_KIND_BATTERY_LOW = 7,  //@alias Battery low @description The battery is low
  SYS_POWER_KIND_BATTERY_CRITICAL = 8,  //@alias Battery critical @description The battery is nearly empty
  SYS_POWER_KIND_BUDGET_EXCEEDED = 9,  //@alias Power budget exceeded @description The rails ask for more power than the source gives
  SYS_POWER_KIND_SOURCE_CHANGED = 10,  //@alias Power source changed @description The board is now fed from another source
  SYS_POWER_KIND_COUNT,
} sys_power_kind_e;

//#ref-enum @alias Power Event Response
typedef enum {
  SYS_POWER_RESPONSE_NOTIFY = 0,  //@alias Notify @description Only raise the event
  SYS_POWER_RESPONSE_DISABLE = 1,  //@alias Disable @description Suspend the affected output: that rail, or every rail for a board-level event
  SYS_POWER_RESPONSE_RESET = 2,  //@alias Reset @description Reset the affected device: that rail, or every rail for a board-level event
  SYS_POWER_RESPONSE_SAFE_STATE = 3,  //@alias Safe State @description Stop the program and suspend every device
} sys_power_response_e;

//#ref-enum @alias Battery Chemistry
typedef enum {
  SYS_POWER_BATTERY_NONE = 0,  //@alias None @description No battery configured
  SYS_POWER_BATTERY_LIPO = 1,  //@alias LiPo @description Lithium polymer, 4.2 V per cell when full
} sys_power_battery_e;

/* ----------------------------------------------------------------------------
 * Events of the manager itself (sys_event.h "Events tables"): contract 0x06, source SYS_DEVICE_ID_NONE, channel 0.
 * The device events it answers (VREG, monitor) are the devices' own.
 * ------------------------------------------------------------------------- */

// clang-format off
#define SYS_POWER_MANAGER_EVENTS(X) \
  X(SOURCE_CHANGED, 0x00, SYS_EVENT_NOTIFY)  /* @alias Source changed @description The board is now fed from another source @value The new source (power source list) */ \
  X(BATTERY_LOW, 0x01, SYS_EVENT_NOTIFY)  /* @alias Battery low @description The battery charge passed the low threshold @value The charge @unit % */ \
  X(BATTERY_CRITICAL, 0x02, SYS_EVENT_NOTIFY)  /* @alias Battery critical @description The battery charge passed the critical threshold @value The charge @unit % */ \
  X(BUDGET_EXCEEDED, 0x03, SYS_EVENT_NOTIFY)  /* @alias Budget exceeded @description The rails ask for more than the budget @value The budget with the margin @unit mW */ \
  X(INPUT_OVP, 0x04, SYS_EVENT_NOTIFY)  /* @alias Input over-voltage @description The input is above the board's maximum @value The input voltage @unit mV */
// clang-format on

#define SYS_POWER_MANAGER__EV(name, n, flags) \
  PWRM_EV_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_POWER_MANAGER, n), PWRM_EV_##name##_FLAGS = (flags),

enum { SYS_POWER_MANAGER_EVENTS(SYS_POWER_MANAGER__EV) };

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

/** @brief The board's power layout. Must stay valid (static). */
typedef struct {
  uint32_t max_mV;              // hardware trip of the input: budget cap; above it INPUT_OVP
  uint32_t max_mA;              // hardware trip of the input: budget cap
  uint32_t reserve_mW;          // input power always taken by fixed loads (the 3.3 V buck)
  uint32_t unknown_source_mA;   // current assumed while the source's limit is unknown (also USB)
  uint8_t vreg_efficiency_pct;  // rail input power = output power * 100 / efficiency
  sys_power_channel_t input;    // measures the board input
  uint16_t usb_pd;              // USB-PD sink device (POWER_USB_PD contract), or SYS_DEVICE_ID_NONE
  const sys_power_source_pins_t* source_pins;  // NULL = none: the source is the "source pin" setting or inferred
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

/** @brief "pwr_cfg": how the manager works. */
typedef struct __attribute__((packed)) {
  uint8_t enabled;  //@alias Enabled @description Off: measure only (status, source and battery events); no budget, no responses @choices 0=No,1=Yes
  uint8_t strict;  //@alias Strict budget @description Strict: the budget is the limit; soft: the budget plus the margin @choices 0=Soft,1=Strict
  uint8_t soft_margin_pct;  //@alias Soft margin @description How far over the budget the rails may go when soft @unit % @min 0 @max 100
  sys_io_pin_t source_pin;  //@alias Source pin @description Tells the battery from USB; none: inferred
  uint8_t battery_level;  //@alias Battery level @description The pin level that means battery @choices 0=Low,1=High
  uint8_t pd_max_auto;  //@alias Negotiate maximum @description On a USB source always ask the charger's highest offer @choices 0=Only_on_request,1=Always
} sys_power_manager_cfg_t;

/** @brief "pwr_psu": an external supply's limits (0 / 0 = none). */
typedef struct __attribute__((packed)) {
  uint32_t max_mV;  //@alias Maximum voltage @description The supply's limit @unit mV
  uint32_t max_mA;  //@alias Maximum current @description The supply's limit @unit mA
} sys_power_psu_t;

/** @brief "pwr_battery". */
typedef struct __attribute__((packed)) {
  uint8_t chemistry;  //@alias Chemistry @enum-ref sys_power_battery_e
  uint8_t cells;  //@alias Cells @description In series
  uint8_t low_pct;  //@alias Low at @description Charge that raises the low event @unit % @min 2 @max 100
  uint8_t critical_pct;  //@alias Critical at @description Charge that raises the critical event @unit % @min 1
} sys_power_battery_t;

/** @brief GET_STATUS answer. */
typedef struct __attribute__((packed)) {
  uint8_t source;  //@alias Source @enum-ref sys_power_source_e
  int32_t input_mV;  //@alias Input voltage @unit mV
  int32_t input_mA;  //@alias Input current @unit mA
  uint32_t source_mV;  //@alias Source voltage limit @description The limit the budget used, after the board's caps @unit mV
  uint32_t source_mA;  //@alias Source current limit @unit mA
  uint32_t budget_mW;  //@alias Budget @description Without the soft margin @unit mW
  uint32_t limit_mW;  //@alias Limit @description What the rails are held to: the budget, or budget plus margin when soft @unit mW
  uint32_t allocated_mW;  //@alias Allocated @description Reserve and rails @unit mW
  uint8_t battery_pct;  //@alias Battery charge @description 0xFF: unknown @unit %
  uint8_t flags;  //@alias Flags @description SYS_POWER_FLAG_* bits
} sys_power_status_t;

#define SYS_POWER_FLAG_ENABLED 0x01u
#define SYS_POWER_FLAG_STRICT 0x02u
#define SYS_POWER_FLAG_OVER_BUDGET 0x04u
#define SYS_POWER_FLAG_INPUT_SEEN 0x08u  // the input monitor answered at least once

#define SYS_POWER_PCT_UNKNOWN 0xFFu

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_POWER_MANAGER)
 * ------------------------------------------------------------------------- */

// clang-format off
#define ERR_SYS_POWER_MANAGER_MAP(X)                                                                                                 \
  X(ERR_PWRM_NOT_READY,              ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 1), ERR_SEV_HIGH,   {})                                     /* before sys_power_manager_init */ \
  X(ERR_PWRM_BUDGET_EXCEEDED,        ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 2), ERR_SEV_MEDIUM, { uint16_t id; uint32_t requested_mW; uint32_t available_mW; }) /* a rail change that doesn't fit */ \
  X(ERR_PWRM_BELOW_ALLOCATION,       ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 3), ERR_SEV_MEDIUM, { uint32_t allocated_mW; uint32_t limit_mW; })              /* the budget shrank under what's committed */ \
  X(ERR_PWRM_FAULT,                  ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 4), ERR_SEV_MEDIUM, { uint16_t source; uint8_t channel; uint8_t kind; uint8_t response; int32_t value; }) /* a hardware fault and the response taken */ \
  X(ERR_PWRM_BAD_ARG,                ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 5), ERR_SEV_HIGH,   { int32_t value; int32_t min; int32_t max; })              /* a setting out of range */ \
  X(ERR_PWRM_NOT_RAIL,               ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 6), ERR_SEV_HIGH,   { uint16_t id; })                                      /* a response target that is neither the board nor a rail of it */ \
  X(ERR_PWRM_NO_OFFER,               ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 7), ERR_SEV_MEDIUM, {})                                     /* no USB-PD sink on the board, or the charger offers nothing */
// clang-format on

ERR_TAGS_DECLARE(ERR_SYS_POWER_MANAGER_MAP)

/* ----------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/** @brief The app's safe state (SAFE_STATE response). Without one: sys_device_suspend_all. */
typedef void (*sys_power_safe_state_f)(void);

/**
 * @brief Starts with the default settings, guards POWER_VREG, subscribes to the rails' and monitors' events and starts the task.
 * Boot step, after sys_event_init. The devices may come later: a missing input monitor is
 * skipped until it answers, a missing rail counts as off.
 */
MUST_USE err_h sys_power_manager_init(const sys_power_board_t* board, sys_power_safe_state_f safe_state);

/** @brief Stored at once, applied on the next tick (woken now). soft_margin_pct 0..100, battery_level 0 / 1. */
MUST_USE err_h sys_power_manager_set_config(const sys_power_manager_cfg_t* cfg);
void sys_power_manager_get_config(sys_power_manager_cfg_t* out);

/** @brief Stored. 0 / 0 removes it. */
MUST_USE err_h sys_power_manager_set_psu(const sys_power_psu_t* psu);
/** @brief Stored. chemistry NONE removes it. */
MUST_USE err_h sys_power_manager_set_battery(const sys_power_battery_t* battery);

/**
 * @brief One response of one target (SYS_POWER_TARGET_BOARD or a rail's device ID). Applied (the app saves the
 * whole matrix as a SET_RESPONSES frame). A rail's row starts from its board-config default.
 * @return ERR_PWRM_NOT_RAIL for another target, ERR_PWRM_BAD_ARG for a kind / response out of range.
 */
MUST_USE err_h sys_power_manager_set_response(uint16_t target, uint8_t kind, uint8_t response);

/**
 * @brief The whole matrix at once: (1 + rails) rows of SYS_POWER_KIND_COUNT responses, row 0 the board, then the
 * rails in the board config's order. Validated and applied; the app saves the SET_RESPONSES frame as the record
 * SYS_ACTIONS_ID_POWER_RESPONSES.
 * @return ERR_CMD_BAD_LEN for another size, ERR_PWRM_BAD_ARG for a response out of range (nothing changes).
 */
MUST_USE err_h sys_power_manager_set_responses(const uint8_t* matrix, size_t len);
MUST_USE err_h sys_power_manager_get_responses(uint16_t target, uint8_t out[SYS_POWER_KIND_COUNT]);

MUST_USE err_h sys_power_manager_get_status(sys_power_status_t* out);

/**
 * @brief Asks the charger for its highest offer, once: its highest voltage and the current offered at it, both capped
 * by the board's max_mV / max_mA. The same the `pd_max_auto` setting does by itself on every USB source.
 * @return ERR_PWRM_NO_OFFER with no USB-PD sink or nothing offered (a plain 5 V USB), the sink's own error otherwise.
 */
MUST_USE err_h sys_power_manager_negotiate_max(void);

#ifdef __cplusplus
}
#endif
