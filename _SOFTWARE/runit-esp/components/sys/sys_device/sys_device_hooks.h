#pragma once

#include "sys_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief How the app wires itself into the registry: the call as a command makes it (the device router), the
 * interceptors and the remote hook. Used by the modules that carry commands (sys_device_router), a policy (the power
 * manager) or a remote device (sys_device_remote); no driver and no block uses them. What happened to a device (created,
 * removed, suspended, resumed, reset) is not a hook: the registry publishes it as an event (SYS_DEVICE).
 */

/* ----------------------------------------------------------------------------
 * Commands
 * ------------------------------------------------------------------------- */

/**
 * @brief sys_device_call for a command (the device router): the same path, and an op marked SYS_DEVICE_OP_LOCAL (its
 * args carry a pointer) is refused on every device, not only on a remote one.
 * @param[in]     id  Device ID
 * @param[in]     dir cmd_dir_e
 * @param[in]     op  Op number, SYS_DEVICE_OP(contract, n)
 * @param[in]     in  The op's payload
 * @param[in,out] out The answer
 * @return ERR_DEV_LOCAL_OP{op}, else as sys_device_call
 */
MUST_USE err_h sys_device_call_routed(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out);

/* ----------------------------------------------------------------------------
 * Interceptors
 * ------------------------------------------------------------------------- */

/**
 * @brief A contract's interceptor: sees every sys_device_call of an op of its contract before the device does, remote
 * commands included. A call is a chain of stages, each taking `in` and giving `out`, and the `out` of one is the `in`
 * of the next: caller -> interceptor -> device -> caller; then the interceptor is told how the call ended
 * (sys_device_intercept_done_f). The interceptor's `out` is the command that goes on to the
 * device (a value clamped to a limit: the device does not know); the device's `out` is the answer that goes back. The
 * interceptor does not run the op: the registry does, with the interceptor's `out`. A policy that can be switched off
 * is a setting of its own module, which the interceptor reads (it passes the call on as it is). Runs without any lock
 * of the registry; it may call other devices (an ordinary nested call). What it decides on is shared by the calls of every device and task (a
 * budget), so it decides and takes under a lock of its own module, and gives back in its `done` stage what a call
 * that failed did not use.
 * @param[in]  id  Device ID
 * @param[in]  dir cmd_dir_e: the read or the write side
 * @param[in]  op  Op number, SYS_DEVICE_OP(contract, n)
 * @param[in]  in  The op's payload as the caller sent it (never changed: it is the caller's buffer)
 * @param[out] out The payload for the device: a buffer the registry gives (`data`, `cap`), `len` is 0 on entry. The
 *                 interceptor fills it and sets `len`, or leaves `len` 0 to pass `in` on as it is (no copy)
 * @return NULL to run the call; an error to refuse it (the device never sees it)
 */
typedef err_h (*sys_device_interceptor_f)(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out);

/**
 * @brief The interceptor's second stage: how the call it let through ended. Called once for every call its first
 * stage returned NULL for, on the same task, after the device (also when the device was never reached: not found,
 * suspended, its lock not free in time). Without any lock of the registry; it may call other devices.
 * @param[in] id     Device ID
 * @param[in] dir    cmd_dir_e
 * @param[in] op     Op number
 * @param[in] passed The payload the device got: the interceptor's `out`, or the caller's `in` when it left `len` 0
 * @param[in] err    The call's error, borrowed (not freed, not kept); NULL: the op ran and worked
 */
typedef void (*sys_device_intercept_done_f)(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t passed, err_h err);

/**
 * @brief Sets the interceptor of `contract`. Boot only; one per contract, up to CONFIG_SYS_DEVICE_INTERCEPTORS.
 * @param[in] contract The contract's ID: its MODULE_ID_<NAME> (MODULE_ID_VREG)
 * @param[in] fn       The first stage: before the device
 * @param[in] done     The second stage: after the device; NULL for an interceptor that keeps nothing per call
 * @return The error when all CONFIG_SYS_DEVICE_INTERCEPTORS are used or the contract has one
 * @usage ERR_TRY(sys_device_set_interceptor(MODULE_ID_VREG, budget_take, budget_settle));
 */
MUST_USE err_h sys_device_set_interceptor(uint8_t contract, sys_device_interceptor_f fn,
                                          sys_device_intercept_done_f done);

/* ----------------------------------------------------------------------------
 * Remote devices
 * ------------------------------------------------------------------------- */

/**
 * @brief The remote hook: carries one call of a remote device to its board, without any lock of the registry (a
 * caller's own lock may be held). The registry has checked the op against the device's type (in the table, not
 * LOCAL, not a write while suspended); the hook builds the device command for that board (`where->remote_id` in place
 * of the local ID) and runs it as `where->flags` says (WIRED: waits for the answer; WIRELESS: SYS_DEVICE_REMOTE.MD).
 * @param where Where the device is: a copy, valid during the call
 * @param dir   cmd_dir_e
 * @param op    Op number, SYS_DEVICE_OP(contract, n)
 * @param op_flags The op's SYS_DEVICE_OP_* of its row (DIRECT: a read never served from a watch's copy)
 * @param in    The op's payload
 * @param out   The answer: a read's value
 * @return NULL; ERR_DEV_REMOTE_DOWN, ERR_WATCH_NO_VALUE (later), the answer's error.
 */
typedef err_h (*sys_device_remote_f)(const sys_device_remote_t* where, uint8_t dir, uint16_t op, uint8_t op_flags,
                                     cmd_in_t in, cmd_out_t* out);

/**
 * @brief Sets the remote hook. Boot only, one (sys_device_remote_init).
 * @param[in] fn The hook; valid for good.
 */
void sys_device_set_remote(sys_device_remote_f fn);

/**
 * @brief The local ID of the remote device that is device `remote_id` of `board`. A snapshot, no lock.
 * @param[in] board     Its board
 * @param[in] remote_id Its ID there
 * @return The local ID, SYS_DEVICE_ID_NONE when no remote device names it
 */
uint8_t sys_device_find_remote(uint8_t board, uint8_t remote_id);

/**
 * @brief Where remote device `id` is. A snapshot, no lock.
 * @param[in]  id    A local device
 * @param[out] where A copy of its record
 * @return false: `id` is not a remote device
 */
bool sys_device_remote_of(uint8_t id, sys_device_remote_t* where);

#ifdef __cplusplus
}
#endif
