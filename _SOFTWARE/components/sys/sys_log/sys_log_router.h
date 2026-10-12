#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Router of ROUTER_KEY_LOG: 0x01 SET {sys_log_cfg_t}, 0x02 GET -> sys_log_cfg_t, 5 bytes (SYS_LOG.MD). */
ROUTER_MODULE_DECLARE(sys_log_router);

#ifdef __cplusplus
}
#endif
