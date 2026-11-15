#pragma once

/*
 * Internal seam between the provider registry (sys_data_provider.c) and its router (router/sys_data_provider_router.c).
 * Not on the component's public include path.
 */

#include "cmd.h"
#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief sys_data_provider_call for a command (sys_data_provider_router): same dispatch as sys_data_provider_call(),
 * and refuses ops marked SYS_DATA_PROVIDER_OP_LOCAL with ERR_PROV_LOCAL_OP.
 */
MUST_USE err_h sys_data_provider_call_routed(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out);
MUST_USE err_h sys_data_provider_bind_commands(uint8_t id);
MUST_USE err_h sys_data_provider_unbind_commands(uint8_t id);

#ifdef __cplusplus
}
#endif
