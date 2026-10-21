#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Router of ROUTER_KEY_SYS: ping, stop, restart, dead-man, bye, clock, safe state (SYS_SYSTEM.MD). */
ROUTER_MODULE_DECLARE(sys_system_router);

#ifdef __cplusplus
}
#endif
