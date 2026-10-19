#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Commands under ROUTER_KEY_POWER (SYS_POWER_MANAGER.MD "Remote"). Never renumber; 0x02, 0x05, 0x06, 0x0A..0x0D are
 * free. A setting's read answers what its write takes. The records SYS_ACTIONS_ID_POWER_* hold the
 * CONFIG, PSU, BATTERY, MATRIX and PD write frames ([ROUTER_KEY_POWER][op][args], SYS_ACTIONS.MD). */
#define SYS_PWRM_OP_CONFIG  0x01u  // sys_power_manager_cfg_t
#define SYS_PWRM_OP_PSU     0x03u  // sys_power_psu_t
#define SYS_PWRM_OP_BATTERY 0x04u  // sys_power_battery_t
#define SYS_PWRM_OP_STATUS  0x07u  // read only: sys_power_status_t
#define SYS_PWRM_OP_PD      0x08u  // sys_power_pd_t: what to ask the USB-PD charger for
#define SYS_PWRM_OP_MATRIX  0x09u  // (1 + rails) rows of sys_power_response_row_t (target, then a response per kind)

/** @brief Router of ROUTER_KEY_POWER (SYS_POWER_MANAGER.MD "Remote"). */
ROUTER_MODULE_DECLARE(sys_power_manager_router);

#ifdef __cplusplus
}
#endif
