#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_POWER (SYS_POWER_MANAGER.MD "Remote"). Never renumber; 0x05 and 0x06 are free. The records SYS_ACTIONS_ID_POWER_* hold the
 * SET_CONFIG, SET_PSU, SET_BATTERY, SET_RESPONSES and SET_PD frames ([ROUTER_KEY_POWER][op][args], SYS_ACTIONS.MD). */
#define SYS_PWRM_OP_SET_CONFIG    0x01u
#define SYS_PWRM_OP_GET_CONFIG    0x02u
#define SYS_PWRM_OP_SET_PSU       0x03u
#define SYS_PWRM_OP_SET_BATTERY   0x04u
#define SYS_PWRM_OP_GET_STATUS    0x07u
#define SYS_PWRM_OP_SET_PD        0x08u  // what to ask the USB-PD charger for (sys_power_pd_t)
#define SYS_PWRM_OP_SET_RESPONSES 0x09u  // the whole matrix: (1 + rails) rows of SYS_POWER_KIND_COUNT responses
#define SYS_PWRM_OP_GET_PSU       0x0Au
#define SYS_PWRM_OP_GET_BATTERY   0x0Bu
#define SYS_PWRM_OP_GET_MATRIX    0x0Cu  // the whole matrix, each row as sys_power_response_row_t (its target first)
#define SYS_PWRM_OP_GET_PD        0x0Du

/** @brief Router of ROUTER_KEY_POWER (SYS_POWER_MANAGER.MD "Remote"). */
ROUTER_MODULE_DECLARE(sys_power_manager_router);

#ifdef __cplusplus
}
#endif
