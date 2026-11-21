#pragma once

/**
 * @file sys_data_provider_internal.h
 * @brief The seam between the registry (sys_data_provider.c: the slots, the locks, the lifecycle) and its router
 * (router/sys_data_provider_router.c: the wire format). Not on the component's public include path.
 */

#include <stdbool.h>
#include <stdint.h>

#include "cmd.h"
#include "errors.h"
#include "sys_data_provider_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One provider as the registry has it now. */
typedef struct {
  uint8_t id;     /**< Provider ID */
  uint8_t type;   /**< sys_data_provider_type_e */
  uint8_t state;  /**< SYS_DATA_PROVIDER_STATE_* */
} sys_data_provider_row_t;

/**
 * @brief Makes provider `id`: its slot, the driver's create, its port in the data matrix.
 * @param id       The provider ID.
 * @param type_id  sys_data_provider_type_e.
 * @param system   true: the board's own (SYS_DATA_PROVIDER_STATE_SYSTEM), not removed.
 * @param cfg      The driver's cfg.
 * @param cfg_len  Its size, the type's cfg size.
 * @return NULL; ERR_PROV_TYPE_UNKNOWN, ERR_CMD_BAD_LEN, ERR_PROV_ID_TAKEN, ERR_FULL, the
 *         driver's create error, the data matrix's
 */
MUST_USE err_h sys_data_provider_create(uint8_t id, uint8_t type_id, bool system, const void* cfg, uint32_t cfg_len);

/**
 * @brief Removes provider `id`: its port out of the data matrix, then its driver, after the call into it in flight.
 * @param id  The provider ID.
 * @return NULL; ERR_PROV_NOT_FOUND, ERR_PROV_SYSTEM
 */
MUST_USE err_h sys_data_provider_remove(uint8_t id);

/**
 * @brief The providers now.
 * @param rows  Room for CONFIG_SYS_DATA_PROVIDER_MAX rows.
 * @return How many rows are written.
 */
uint32_t sys_data_provider_list(sys_data_provider_row_t* rows);

/**
 * @brief Runs a driver's own op on provider `id`, one call into the driver at a time.
 * @param id   The provider ID.
 * @param dir  CMD_DIR_READ or CMD_DIR_WRITE.
 * @param op   SYS_DATA_PROVIDER_OWN_OP(n).
 * @param in   Its args.
 * @param out  Its answer.
 * @return The op's; ERR_PROV_NOT_FOUND, ERR_PROV_UNKNOWN_OP (no such op or side)
 */
MUST_USE err_h sys_data_provider_own_op(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out);

#ifdef __cplusplus
}
#endif
