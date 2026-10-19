#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_POWER (SYS_POWER_MANAGER.MD "Remote"). Never renumber. The records SYS_ACTIONS_ID_POWER_* hold the
 * SET_CONFIG, SET_PSU, SET_BATTERY and SET_RESPONSES frames ([ROUTER_KEY_POWER][op][args], SYS_ACTIONS.MD). */
#define SYS_PWRM_OP_SET_CONFIG 0x01u
#define SYS_PWRM_OP_GET_CONFIG 0x02u
#define SYS_PWRM_OP_SET_PSU 0x03u
#define SYS_PWRM_OP_SET_BATTERY 0x04u
#define SYS_PWRM_OP_SET_RESPONSE 0x05u
#define SYS_PWRM_OP_GET_RESPONSES 0x06u
#define SYS_PWRM_OP_GET_STATUS 0x07u
#define SYS_PWRM_OP_NEGOTIATE_MAX 0x08u
#define SYS_PWRM_OP_SET_RESPONSES 0x09u  // the whole matrix: (1 + rails) rows of SYS_POWER_KIND_COUNT responses
#define SYS_PWRM_OP_GET_PSU 0x0Au
#define SYS_PWRM_OP_GET_BATTERY 0x0Bu
#define SYS_PWRM_OP_GET_MATRIX 0x0Cu  // the whole matrix, each row as sys_power_response_row_t (its target first)

/** @brief Router of ROUTER_KEY_POWER (SYS_POWER_MANAGER.MD "Remote"). */
ROUTER_MODULE_DECLARE(sys_power_manager_router);

#ifdef __cplusplus
}
#endif
