#pragma once

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates the runIT service (0xFFE0: TX 0xFFE1, RX 0xFFE2, LOGS 0xFFE3,
 * STATUS 0xFFE4), locks it and registers TRANSPORT_BLE as the router link
 * (framed writes on RX are reassembled and submitted, answers go out on TX)
 * and as the connector provider (endpoint = characteristic UUID). Call after
 * router_init() and sys_data_connector_init(), before sys_ble_start().
 */
MUST_USE err_h sys_ble_link_init(void);

#ifdef __cplusplus
}
#endif
