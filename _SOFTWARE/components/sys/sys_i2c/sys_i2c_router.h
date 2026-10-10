#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Remote diagnostics of the I2C buses, under ROUTER_KEY_I2C.
 * Scan / reset / raw access only on CONFIG_SYS_I2C_REMOTE_BUS; raw access
 * only with CONFIG_SYS_I2C_REMOTE_RAW and only at addresses with no device.
 * Ops and layouts: SYS_I2C.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_I2C, sys_i2c_router));
 */
ROUTER_MODULE_DECLARE(sys_i2c_router);

#ifdef __cplusplus
}
#endif
