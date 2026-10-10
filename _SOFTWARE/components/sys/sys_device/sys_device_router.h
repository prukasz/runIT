#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Router of ROUTER_KEY_DEVICE: [id u16 LE][op u16 LE][args] (SYS_DEVICE.MD).
 * The remote interface of the registry, nothing else: the registry
 * (sys_device.h) and everything built on it don't include the router, only the
 * app that registers this one.
 * @usage ERR_TRY(router_register(ROUTER_KEY_DEVICE, sys_device_router));
 */
ROUTER_MODULE_DECLARE(sys_device_router);

#ifdef __cplusplus
}
#endif
