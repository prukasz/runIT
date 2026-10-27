#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Data connectors under ROUTER_KEY_CONN: create, bind, suspend, info.
 * Ops and layouts: SYS_DATA_CONNECTOR.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_CONN, sys_data_connector_router));
 */
ROUTER_MODULE_DECLARE(sys_data_connector_router);

#ifdef __cplusplus
}
#endif
