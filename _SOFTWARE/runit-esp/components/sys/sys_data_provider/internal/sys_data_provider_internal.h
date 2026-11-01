#pragma once

/*
The provider's own router (router/sys_data_provider_router.c) and nothing else: LIST and SEND. Not on the component's
public include path.
*/

#include "sys_data_provider_router.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Copies every provider out as LIST sends it; returns how many. */
uint8_t sys_data_provider_list(sys_data_provider_info_t out[CONFIG_SYS_DATA_PROVIDER_MAX]);

/**
 * @brief SEND: one frame on a provider no user has opened (an open one is its user's).
 * @return NULL when queued; ERR_PROV_IN_USE, or what sys_data_provider_send returns.
 */
MUST_USE err_h sys_data_provider_send_free(sys_data_provider_id_t id, const uint8_t* data, uint32_t len);

#ifdef __cplusplus
}
#endif
