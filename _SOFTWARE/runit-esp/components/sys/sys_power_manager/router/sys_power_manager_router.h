#pragma once

#include "router_table.h"
#include "sys_power_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The power manager's commands, under the root key MODULE_ID_PWRM: its own settings, so the frames stay with its API
 * (sys_power_manager.h). A setting's read answers what its write takes. The router registers itself. The records
 * SYS_ACTIONS_ID_POWER_* hold the CONFIG, PSU, BATTERY, MATRIX and PD write frames ([MODULE_ID_PWRM][op][args]).
 *
 * The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h). Sides: CONFIG RW, LAYOUT W (from the
 * board's profile), PSU RW, BATTERY RW, RAIL W (from the board's profile), STATUS R, PD RW, MATRIX RW (the value is
 * one row: the frame is 1 + rails of them, target first, then a response per kind). 0x06, 0x0A..0x0D are free.
 */
#define PWRM_OPERATIONS(X, P)                                     \
  X(P, OP_CONFIG,  0x01, cmd_none_t, sys_power_manager_cfg_t,  0) \
  X(P, OP_LAYOUT,  0x02, cmd_none_t, sys_power_layout_t,       0) \
  X(P, OP_PSU,     0x03, cmd_none_t, sys_power_psu_t,          0) \
  X(P, OP_BATTERY, 0x04, cmd_none_t, sys_power_battery_t,      0) \
  X(P, OP_RAIL,    0x05, cmd_none_t, sys_power_rail_t,         0) \
  X(P, OP_STATUS,  0x07, cmd_none_t, sys_power_status_t,       0) \
  X(P, OP_PD,      0x08, cmd_none_t, sys_power_pd_t,           0) \
  X(P, OP_MATRIX,  0x09, cmd_none_t, sys_power_response_row_t, 0)

CMD_TABLE_DECLARE(PWRM)

/** @brief Router of the root key MODULE_ID_PWRM. */
ROUTER_MODULE_DECLARE(sys_power_manager_router);

#ifdef __cplusplus
}
#endif
