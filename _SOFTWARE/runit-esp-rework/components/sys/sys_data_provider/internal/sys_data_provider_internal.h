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
 * @brief A command of the provider contract or a driver's own op (sys_data_provider_router): CREATE, REMOVE, LIST,
 * INFO, OPS, or the op of the provider's type.
 * @param id   The provider (SYS_DATA_PROVIDER_ID_NONE for LIST).
 * @param dir  CMD_DIR_READ / CMD_DIR_WRITE.
 * @param op   The op code (SYS_DATA_PROVIDER_OP / _OWN_OP).
 * @param in   Its args.
 * @param out  The answer, may be NULL for a write.
 * @return NULL; ERR_PROV_* of the op, ERR_CMD_* of its frame
 */
MUST_USE err_h sys_data_provider_call_routed(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out);

#ifdef __cplusplus
}
#endif
