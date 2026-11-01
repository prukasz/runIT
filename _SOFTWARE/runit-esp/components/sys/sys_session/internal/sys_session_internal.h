#pragma once

/*
The session's own router (router/sys_session_router.c) and nothing else: PEERS. Not on the component's public
include path.
*/

#include "sdkconfig.h"
#include "sys_session_router.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Copies every session out as PEERS sends it; returns how many. */
uint8_t sys_session_list(sys_session_info_t out[CONFIG_SYS_SESSION_MAX]);

#ifdef __cplusplus
}
#endif
