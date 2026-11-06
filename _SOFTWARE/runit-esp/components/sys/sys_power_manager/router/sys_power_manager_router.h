#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_POWER (SYS_POWER_MANAGER.MD "Remote"), with their sides; the frames are the packed structs of
 * sys_power_manager.h (little-endian; contract_power_manager.json names them): they are the module's own settings, so
 * they stay with its API. Never renumber; 0x06, 0x0A..0x0D are free. A setting's read answers what its write takes.
 * The records SYS_ACTIONS_ID_POWER_* hold the CONFIG, PSU, BATTERY, MATRIX and PD write frames
 * ([ROUTER_KEY_POWER][op][args], SYS_ACTIONS.MD). */
#define SYS_PWRM_OP_CONFIG  0x01u  // R: -> sys_power_manager_cfg_t; W: sys_power_manager_cfg_t
#define SYS_PWRM_OP_LAYOUT  0x02u  // W: sys_power_layout_t, from the board's profile
#define SYS_PWRM_OP_PSU     0x03u  // R: -> sys_power_psu_t; W: sys_power_psu_t
#define SYS_PWRM_OP_BATTERY 0x04u  // R: -> sys_power_battery_t; W: sys_power_battery_t
#define SYS_PWRM_OP_RAIL    0x05u  // W: sys_power_rail_t, from the board's profile
#define SYS_PWRM_OP_STATUS  0x07u  // R: -> sys_power_status_t
#define SYS_PWRM_OP_PD      0x08u  // R: -> sys_power_pd_t; W: sys_power_pd_t, what to ask the USB-PD charger for
#define SYS_PWRM_OP_MATRIX  0x09u  // R: -> (1 + rails) rows of sys_power_response_row_t; W: the same rows (target, then a response per kind)

/** @brief Router of ROUTER_KEY_POWER (SYS_POWER_MANAGER.MD "Remote"). */
ROUTER_MODULE_DECLARE(sys_power_manager_router);

#ifdef __cplusplus
}
#endif
