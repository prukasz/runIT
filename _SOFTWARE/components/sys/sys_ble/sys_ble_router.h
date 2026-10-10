#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Runtime GATT table and BLE status, under ROUTER_KEY_BLE. Ops and
 * layouts: SYS_BLE.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_BLE, sys_ble_router));
 */
ROUTER_MODULE_DECLARE(sys_ble_router);

#ifdef __cplusplus
}
#endif
