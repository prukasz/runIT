#pragma once

#include <stdint.h>

#include "errors.h"
#include "sys_device_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Private to the registry (sys_device_new): what sys_device_call needs of the registry's own part (types,
 * CREATE, the registry's ops). The caller holds the lock of the slot `id`.
 */

/**
 * @brief The side `dir` of a row.
 * @param[in] row The op's row, or NULL
 * @param[in] dir cmd_dir_e
 * @return The handler, NULL when there is no row or no such side
 */
static inline cmd_f sys_device_op_side(const sys_device_op_t* row, uint8_t dir) {
  return row == NULL ? NULL : dir == CMD_DIR_READ ? row->read : row->write;
}

/**
 * @brief The row of `op` in the type of device `id`: for a local device when it runs it as wired (has_op), for a
 * remote one any row of the table (its board knows its wiring).
 * @param[in] id Device ID, installed
 * @param[in] op Op number
 * @return The row, NULL when the device has no such op or does not exist
 */
const sys_device_op_t* sys_device_type_op(uint8_t id, uint16_t op);

/**
 * @brief The registry's row of `op` (SUSPEND, RESUME, REMOVE, INFO, OPS): the same on every device, never in a type's
 * table. Its handlers take the device's ID as ctx ((void*)(uintptr_t)id). CREATE is not one: it has no device yet.
 * @param[in] op Op number
 * @return The row, NULL when `op` is not the registry's
 */
const sys_device_op_t* sys_device_registry_op(uint16_t op);

/**
 * @brief CREATE of slot `id`: [type u16][flags u8][create struct]. The type's create() runs with the slot's lock held,
 * the slot is installed when it worked.
 * @param[in] id Device ID, not installed
 * @param[in] in The args
 * @return ERR_CMD_BAD_LEN, ERR_DEV_BAD_ARG (flags), ERR_DEV_TYPE_UNKNOWN, ERR_DEV_ID_TAKEN, ERR_DEV_NO_MEM, or the
 *         create() error wrapped in ERR_DEV_INSTALL
 */
MUST_USE err_h sys_device_registry_create(uint8_t id, cmd_in_t in);

/**
 * @brief A registered type.
 * @param[in] id Type ID, as its driver declared it (SYS_DEVICE_TYPE_DECLARE)
 * @return The type, NULL when no driver registered it
 */
const sys_device_type_t* sys_device_type_find(uint16_t id);

/**
 * @brief The ID a type was registered with (INFO's type_id).
 * @param[in] type A registered type
 * @return Its ID, 0 for a type never registered
 */
uint16_t sys_device_type_id(const sys_device_type_t* type);

#ifdef __cplusplus
}
#endif
