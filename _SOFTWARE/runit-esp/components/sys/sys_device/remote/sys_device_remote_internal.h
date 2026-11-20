#pragma once

#include <stdint.h>

#include "errors.h"
#include "sys_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Private to the registry (sys_device): remote devices, a local ID for a device of another board. The slot
 * keeps its type and, as the instance, where it is (sys_device_remote_t); its calls go to the hook sys_device_remote sets.
 */

/**
 * @brief CREATE_REMOTE of slot `id`: [type u16][sys_device_remote_t]. No create(): the device is built on its board,
 * this one keeps its type and where it is (the instance). The caller holds the lock of the slot.
 * @param[in] id Device ID, not installed
 * @param[in] in The args
 * @return ERR_CMD_BAD_LEN, ERR_DEV_BAD_ARG (remote_id, flags), ERR_DEV_TYPE_UNKNOWN, ERR_DEV_ID_TAKEN (this ID, or
 *         that device has a local ID already), ERR_DEV_NO_MEM
 */
MUST_USE err_h sys_device_remote_create(uint8_t id, cmd_in_t in);

/**
 * @brief Sends a checked call of remote device `id` to its board through the remote hook. Without the slot's lock.
 * @param[in]  id    Device ID (for the error)
 * @param[in]  where Where it is: a copy taken under the lock
 * @param[in]  dir   cmd_dir_e
 * @param[in]  op    Op number
 * @param[in]  op_flags The op's SYS_DEVICE_OP_* (DIRECT: a read the hook sends and waits for)
 * @param[in]  in    The op's payload
 * @param[out] out   The answer
 * @return ERR_DEV_REMOTE_DOWN{id, board} without a hook (no sys_device_remote on this board), else the hook's
 */
MUST_USE err_h sys_device_remote_send(uint8_t id, const sys_device_remote_t* where, uint8_t dir, uint16_t op,
                                      uint8_t op_flags, cmd_in_t in, cmd_out_t* out);

#ifdef __cplusplus
}
#endif
