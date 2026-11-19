#pragma once

/*
Internal interface between sys_session.c and router/. Not on the component's public include path.
*/

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "sys_session.h"
#include "sys_session_router.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Copies every session out as PEERS sends it; returns how many. */
uint8_t sys_session_list(sys_session_info_t out[CONFIG_SYS_SESSION_MAX]);

#ifdef __cplusplus
}
#endif
