/* The power layouts of the boards as C (BOARDS.MD): not compiled, what a board's profile must give the power manager
 * (LAYOUT and RAIL, SYS_POWER_MANAGER.MD). */

#include "devices.h"
#include "runit_r1_defs.h"
#include "sys_power_manager.h"

/* ---- Power layout ---- */

/* Default response per power event; changed at runtime with SET_RESPONSES. */
static const uint8_t k_power_responses[SYS_POWER_KIND_COUNT] = {
    [SYS_POWER_KIND_OVP] = SYS_POWER_RESPONSE_DISABLE,
    [SYS_POWER_KIND_UVP] = SYS_POWER_RESPONSE_NOTIFY,
    [SYS_POWER_KIND_SPC] = SYS_POWER_RESPONSE_DISABLE,
    [SYS_POWER_KIND_OCP_WARNING] = SYS_POWER_RESPONSE_NOTIFY,
    [SYS_POWER_KIND_OCP_CRITICAL] = SYS_POWER_RESPONSE_NOTIFY,  // bring-up: a rail in current limit keeps running
    [SYS_POWER_KIND_OTP] = SYS_POWER_RESPONSE_SAFE_STATE,
    [SYS_POWER_KIND_BATTERY_LOW] = SYS_POWER_RESPONSE_NOTIFY,
    [SYS_POWER_KIND_BATTERY_CRITICAL] = SYS_POWER_RESPONSE_SAFE_STATE,
    [SYS_POWER_KIND_BUDGET_EXCEEDED] = SYS_POWER_RESPONSE_DISABLE,
    [SYS_POWER_KIND_SOURCE_CHANGED] = SYS_POWER_RESPONSE_NOTIFY,
};

#if CONFIG_RUNIT_BOARD_DEVKIT

/* A devkit on USB: no rails, no monitor, no sink, no source pins; only the input limits for the budget. */
static const sys_power_board_t k_power_board = {
    .max_mV = RUNIT_DEVKIT_POWER_LIMIT_MV,
    .max_mA = RUNIT_DEVKIT_POWER_LIMIT_MA,
    .reserve_mW = RUNIT_DEVKIT_POWER_RESERVE_MW,
    .unknown_source_mA = RUNIT_DEVKIT_POWER_LIMIT_MA,
    .vreg_efficiency_pct = RUNIT_BOARD_VREG_EFFICIENCY_PCT,
    .input = {.device = SYS_DEVICE_ID_NONE},
    .usb_pd = SYS_DEVICE_ID_NONE,
    .responses = k_power_responses,
};

#else

/* Power layout of this board revision: both TPS55289 rails budgeted, the INA3221 measures them and the input. */
static const sys_power_consumer_t k_power_consumers[] = {
    {.vreg = DEVICE_ID_TPS55289_2,
     .monitor = {.device = DEVICE_ID_INA3221, .channel = RUNIT_BOARD_INA_CH_RAIL_A},
     .min_mA = TPS55289_MIN_CURRENT_MA},
    {.vreg = DEVICE_ID_TPS55289_1,
     .monitor = {.device = DEVICE_ID_INA3221, .channel = RUNIT_BOARD_INA_CH_RAIL_B},
     .min_mA = TPS55289_MIN_CURRENT_MA},
};

/* The eFuse power-good outputs on the TCA6424A: VUSB_OK 3, VEXT_OK 4 (HARDWARE.md). They are inputs after the TCA6424A is created. */
static const sys_power_source_pins_t k_source_pins = {
    .usb_ok = {.device = DEVICE_ID_TCA6424A, .pin = RUNIT_BOARD_TCA_VUSB_OK},
    .ext_ok = {.device = DEVICE_ID_TCA6424A, .pin = RUNIT_BOARD_TCA_VEXT_OK},
};

static const sys_power_board_t k_power_board = {
    .max_mV = RUNIT_BOARD_POWER_LIMIT_MV,
    .max_mA = RUNIT_BOARD_POWER_LIMIT_MA,
    .reserve_mW = RUNIT_BOARD_POWER_RESERVE_MW,
    .unknown_source_mA = RUNIT_BOARD_POWER_UNKNOWN_SOURCE_MA,
    .vreg_efficiency_pct = RUNIT_BOARD_VREG_EFFICIENCY_PCT,
    .input = {.device = DEVICE_ID_INA3221, .channel = RUNIT_BOARD_INA_CH_INPUT},
    .usb_pd = DEVICE_ID_AP33772S,
    .source_pins = &k_source_pins,
    .consumers = k_power_consumers,
    .consumer_count = ARRAY_LEN(k_power_consumers),
    .responses = k_power_responses,
};

#endif
