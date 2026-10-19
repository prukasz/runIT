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

/* What feeds the external input. The board's eFuse only says the input is live, not what is on it, so the user says. */
//#ref-enum @alias External input source
typedef enum {
  SYS_POWER_EXT_BENCH = 0,    //@alias Bench supply @description Bench power supply
  SYS_POWER_EXT_BATTERY = 1,  //@alias Battery pack @description Battery pack
} sys_power_ext_input_e;

/* ----------------------------------------------------------------------------
 * Events of the manager itself (sys_event.h "Events tables"): contract 0x06, source SYS_DEVICE_ID_NONE, channel 0.
 * The device events it answers (VREG, monitor) are the devices' own.
 * ------------------------------------------------------------------------- */

#define SYS_POWER_MANAGER_EVENTS(X)           \
  X(SOURCE_CHANGED,   0x00, SYS_EVENT_NOTIFY) \
  X(BATTERY_LOW,      0x01, SYS_EVENT_NOTIFY) \
  X(BATTERY_CRITICAL, 0x02, SYS_EVENT_NOTIFY) \
  X(BUDGET_EXCEEDED,  0x03, SYS_EVENT_NOTIFY) \
  X(INPUT_OVP,        0x04, SYS_EVENT_NOTIFY)

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

/** @brief How the manager works (SET_CONFIG / GET_CONFIG). */
typedef struct __attribute__((packed)) {
  uint8_t enabled;          /**< 1 = enabled, 0 = measure only */
  uint8_t strict;           /**< 1 = strict budget, 0 = soft margin */
  uint8_t soft_margin_pct;  /**< Soft margin above budget (0..100%) */
  sys_io_pin_t source_pin;  /**< Pin to distinguish battery from USB */
  uint8_t battery_level;    /**< Pin level corresponding to battery */
  uint8_t pd_max_auto;      /**< 1 = auto negotiate max USB-PD offer */
  uint8_t ext_input;        /**< sys_power_ext_input_e */
} sys_power_manager_cfg_t;

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

/** @brief One row of the response matrix with its target (GET_MATRIX): the board (SYS_POWER_TARGET_BOARD) or a rail. */
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
} sys_power_status_t;

#define SYS_POWER_FLAG_ENABLED     0x01u
#define SYS_POWER_FLAG_STRICT      0x02u
#define SYS_POWER_FLAG_OVER_BUDGET 0x04u
#define SYS_POWER_FLAG_INPUT_SEEN  0x08u  // the input monitor answered at least once

#define SYS_POWER_PCT_UNKNOWN 0xFFu

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_POWER_MANAGER)
 * ------------------------------------------------------------------------- */

#define ERR_SYS_POWER_MANAGER_MAP(X)                                                                                                                                                                                            \
  X(ERR_PWRM_NOT_READY,              ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 1), ERR_SEV_HIGH,   {})                                     /* before sys_power_manager_init */                                                         \
  X(ERR_PWRM_BUDGET_EXCEEDED,        ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 2), ERR_SEV_MEDIUM, { uint16_t id; uint32_t requested_mW; uint32_t available_mW; }) /* a rail change that doesn't fit */                                \
  X(ERR_PWRM_BELOW_ALLOCATION,       ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 3), ERR_SEV_MEDIUM, { uint32_t allocated_mW; uint32_t limit_mW; })              /* the budget shrank under what's committed */                          \
  X(ERR_PWRM_FAULT,                  ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 4), ERR_SEV_MEDIUM, { uint16_t source; uint8_t channel; uint8_t kind; uint8_t response; int32_t value; }) /* a hardware fault and the response taken */ \
  X(ERR_PWRM_BAD_ARG,                ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 5), ERR_SEV_HIGH,   { int32_t value; int32_t min; int32_t max; })              /* a setting out of range */                                             \
  X(ERR_PWRM_NOT_RAIL,               ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 6), ERR_SEV_HIGH,   { uint16_t id; })                                      /* a response target that is neither the board nor a rail of it */           \
  X(ERR_PWRM_NO_OFFER,               ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 7), ERR_SEV_MEDIUM, {})                                     /* no USB-PD sink on the board, or the charger offers nothing */

ERR_TAGS_DECLARE(ERR_SYS_POWER_MANAGER_MAP)

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

/** @brief Stored at once, applied on the next tick (woken now). soft_margin_pct 0..100, battery_level 0 / 1. */
MUST_USE err_h sys_power_manager_set_config(const sys_power_manager_cfg_t* cfg);
void sys_power_manager_get_config(sys_power_manager_cfg_t* out);

/** @brief Stored. 0 / 0 removes it. */
MUST_USE err_h sys_power_manager_set_psu(const sys_power_psu_t* psu);
/** @brief Stored. chemistry NONE removes it. */
MUST_USE err_h sys_power_manager_set_battery(const sys_power_battery_t* battery);
/** @brief The external supply as set (0 / 0 = none). */
void sys_power_manager_get_psu(sys_power_psu_t* out);
/** @brief The battery as set (chemistry NONE = none). */
void sys_power_manager_get_battery(sys_power_battery_t* out);

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

/**
 * @brief The whole matrix, each row with its target: the board first, then the rails in the board config's order (the
 * order SET_RESPONSES takes, without the targets). `cap` rows at most; `*count` gets the number written.
 * @return ERR_PWRM_NOT_READY before init, ERR_CMD_BAD_LEN when `cap` is too small.
 */
MUST_USE err_h sys_power_manager_get_matrix(sys_power_response_row_t* rows, uint8_t cap, uint8_t* count);

MUST_USE err_h sys_power_manager_get_status(sys_power_status_t* out);

/**
 * @brief Asks the charger for its highest offer, once: its highest voltage and the current offered at it, both capped
 * by the board's max_mV / max_mA. The same the `pd_max_auto` setting does by itself on every USB source.
 * @return ERR_PWRM_NO_OFFER when there is no USB-PD sink, the sink cannot be read, or it offers nothing (a plain 5 V USB).
 */
MUST_USE err_h sys_power_manager_negotiate_max(void);

#ifdef __cplusplus
}
#endif
