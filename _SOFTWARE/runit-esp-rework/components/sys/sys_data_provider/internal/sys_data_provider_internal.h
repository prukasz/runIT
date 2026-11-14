#pragma once

/*
The provider's own router (router/sys_data_provider_router.c) and nothing else: LIST. Not on the component's
public include path.
*/

#include "sys_data_provider_router.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Copies every provider out as LIST sends it; returns how many. */
uint8_t sys_data_provider_list(sys_data_provider_info_t out[CONFIG_SYS_DATA_PROVIDER_MAX]);

#ifdef __cplusplus
}
#endif
