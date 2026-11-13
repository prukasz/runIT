#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"
#include "sys_device_driver.h"

_Static_assert(SYS_DEVICE_COUNT <= 64, "a dependency mask has one bit per device");

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Private to the registry (sys_device_new): nothing else includes this. The device slots: a static table
 * indexed by the device ID, each slot with its own lock, its flags, its resources and the masks of the devices it
 * uses. The table knows no driver: it keeps the type and the instance the registry gives it. Creating a device is the
 * registry's: it takes the lock, builds the instance, then sys_device_slot_install().
 */

/**
 * @brief Takes the lock of slot `id` for op `op`, waiting at most CONFIG_SYS_DEVICE_LOCK_TIMEOUT_MS. Not recursive:
 * the task that holds it must not take it again. The slot remembers `op` while it is locked, for the error of whoever
 * waits too long.
 * @param[in] id Device ID
 * @param[in] op The op the lock is taken for
 * @return ERR_DEV_BAD_ID{id} past SYS_DEVICE_COUNT, ERR_DEV_REENTRY{id} when this task holds the lock already,
 *         ERR_DEV_BUSY{id, op} when another task still held it after the timeout (`op`: the op that task runs)
 */
MUST_USE err_h sys_device_slot_lock(uint8_t id, uint16_t op);

/**
 * @brief Takes the lock of slot `id` for op `op` only when it is free now: no wait, no error (a step, which comes again
 * on its own time).
 * @param[in] id Device ID
 * @param[in] op The op the lock is taken for
 * @return true when taken; false past SYS_DEVICE_COUNT or when any task (this one too) holds it
 */
bool sys_device_slot_try_lock(uint8_t id, uint16_t op);

/**
 * @brief Gives back the lock of slot `id`, held by this task.
 * @param[in] id Device ID
 */
void sys_device_slot_unlock(uint8_t id);

/* Getters: any task, no lock; with the lock of the slot held they are exact, without it a snapshot. An ID past
 * SYS_DEVICE_COUNT is not installed. */

/** @brief Whether the device exists. */
bool sys_device_slot_is_installed(uint8_t id);

/** @brief Whether the device is in the safe state (installed and suspended). */
bool sys_device_slot_is_suspended(uint8_t id);

/** @brief Whether the device takes calls (installed and not suspended). */
bool sys_device_slot_is_active(uint8_t id);

/** @brief Whether the device is the board's own (SYS_DEVICE_CREATE_SYSTEM): never removed. */
bool sys_device_slot_is_system(uint8_t id);

/** @brief Whether the device is on another board: its instance is a sys_device_remote_t, its calls go through the
 * remote hook. */
bool sys_device_slot_is_remote(uint8_t id);

/** @brief The device's type; NULL when it is not installed. */
const sys_device_type_t* sys_device_slot_type(uint8_t id);

/** @brief The driver's struct (a remote device's sys_device_remote_t); NULL when it is not installed or has none. */
void* sys_device_slot_instance(uint8_t id);

/** @brief The slot's generation: counts the devices the slot has had (1..255, then 1 again; 0 before the first) and
 * stays when the slot is freed, so the device in a slot and the one before it differ by it. A CREATE takes a new one
 * before the device is built, so create() sees its own. 0 past SYS_DEVICE_COUNT. */
uint8_t sys_device_slot_gen(uint8_t id);

/** @brief Bit i set: slot i holds a device. A snapshot. */
uint64_t sys_device_slot_installed(void);

/* Setters: the caller holds the lock of the slot (sys_device_slot_lock), else ERR_DEV_NOT_LOCKED{id}. */

/**
 * @brief The first step of CREATE and CREATE_REMOTE, before the device is built: the slot takes a new generation, so
 * what create() hands out by ID and generation (a timer) names this device and none before it. A CREATE that fails
 * after it leaves the generation used.
 * @param[in] id Device ID, not installed
 * @return ERR_DEV_BAD_ID{id} past SYS_DEVICE_COUNT, ERR_DEV_ID_TAKEN{id} when the device exists
 */
MUST_USE err_h sys_device_slot_begin_create(uint8_t id);

/**
 * @brief Makes slot `id` a device: sets the type and the instance, then `installed` last. The generation is the one
 * sys_device_slot_begin_create() took.
 * @param[in] id       Device ID, not installed
 * @param[in] type     The device's type
 * @param[in] instance The driver's struct; for a remote device its sys_device_remote_t
 * @param[in] system   true: the board's own device, never removed
 * @param[in] remote   true: a device of another board
 * @return ERR_DEV_BAD_ID{id} past SYS_DEVICE_COUNT, ERR_DEV_ID_TAKEN{id} when the device exists already
 */
MUST_USE err_h sys_device_slot_install(uint8_t id, const sys_device_type_t* type, void* instance, bool system,
                                       bool remote);

/**
 * @brief The first step of REMOVE, in one critical section with the declaring of a use (sys_device_slot_use): when no
 * device uses `id` it is marked as being removed, and from then on no device can declare it uses it.
 * @param[in]  id    Device ID, installed, its lock held
 * @param[out] users When refused: bit i set, the device in slot i uses `id`
 * @return true: marked, the removal goes on; false: other devices use it (`*users`), nothing changed
 */
bool sys_device_slot_begin_remove(uint8_t id, uint64_t* users);

/**
 * @brief Makes slot `id` free: the devices it used are free of it, `installed` is cleared first, then the rest. The
 * lock and the generation stay. A free slot is left as it is.
 * @param[in] id Device ID
 * @return ERR_DEV_BAD_ID{id} past SYS_DEVICE_COUNT
 */
MUST_USE err_h sys_device_slot_uninstall(uint8_t id);

/**
 * @brief Sets or clears the safe state of an installed device.
 * @param[in] id        Device ID
 * @param[in] suspended true: the safe state
 * @return ERR_DEV_BAD_ID{id} past SYS_DEVICE_COUNT, ERR_DEV_NOT_FOUND{id} when the device is not installed
 */
MUST_USE err_h sys_device_slot_set_suspended(uint8_t id, bool suspended);

/* Calls in flight: the NOLOCK ops (SYS_DEVICE_OP_NOLOCK) that run without the lock of the slot. Counted under a short
 * critical section of their own, so a call leaves without the lock. */

/**
 * @brief Counts a NOLOCK call in flight on an installed device. The caller holds the lock of the slot (it made the
 * call's checks under it) and lets it go right after.
 * @param[in] id Device ID
 * @return ERR_DEV_BAD_ID{id} past SYS_DEVICE_COUNT, ERR_DEV_NOT_FOUND{id} when the device is not installed
 */
MUST_USE err_h sys_device_slot_enter(uint8_t id);

/**
 * @brief The NOLOCK call counted by sys_device_slot_enter() returned. Any task, no lock.
 * @param[in] id Device ID
 */
void sys_device_slot_leave(uint8_t id);

/**
 * @brief Waits until no NOLOCK call is in flight on the device. The caller holds the lock of the slot, so no new one
 * starts: REMOVE, before the driver's struct is released.
 * @param[in] id Device ID
 */
void sys_device_slot_drain(uint8_t id);

/* Resources: what the device took (sys_device_res_add), a list in the slot, the last added first. Guarded by the lock
 * of the slot alone: only its holder adds and releases. */

/**
 * @brief Adds `res` to the resources of slot `id`, installed or being created. The caller holds the lock of the slot.
 * @param[in] id  Device ID
 * @param[in] res The node, filled (release, arg) and not in a list
 * @return ERR_DEV_BAD_ID{id} past SYS_DEVICE_COUNT, ERR_DEV_NOT_LOCKED{id} without the lock
 */
MUST_USE err_h sys_device_slot_res_push(uint8_t id, sys_device_res_t* res);

/**
 * @brief Takes the last added resource of slot `id` out of its list. The caller holds the lock of the slot.
 * @param[in] id Device ID
 * @return The node, NULL when the slot has none left
 */
sys_device_res_t* sys_device_slot_res_pop(uint8_t id);

/* Dependencies: one bit per slot. The masks are guarded by a short critical section of their own, entered after the
 * lock of a slot. */

/**
 * @brief Declares that device `user` uses device `dep`: sets the bit in both masks. Declaring it twice is fine.
 * The caller holds the lock of `user` (it is in create(), or in an op of `user`).
 * @param[in] user The device that uses `dep`
 * @param[in] dep  The device used: installed, not being removed, and not `user`
 * @return ERR_DEV_BAD_ID{user} past SYS_DEVICE_COUNT, ERR_DEV_NOT_LOCKED{user}, ERR_DEV_NOT_FOUND{dep} unless `dep`
 *         is installed and stays, ERR_DEV_CYCLE{user, dep} when `dep` uses `user` already, itself or through others
 */
MUST_USE err_h sys_device_slot_use(uint8_t user, uint8_t dep);

/**
 * @brief Takes back a sys_device_slot_use(): clears the bit in both masks. A dependency not declared is left as it is.
 * @param[in] user The device that used `dep`, its lock held
 * @param[in] dep  The device used
 * @return ERR_DEV_BAD_ID{id} past SYS_DEVICE_COUNT for either, ERR_DEV_NOT_LOCKED{user}
 */
MUST_USE err_h sys_device_slot_unuse(uint8_t user, uint8_t dep);

/** @brief Bit i set: device `id` uses the device in slot i. 0 for an ID past SYS_DEVICE_COUNT. */
uint64_t sys_device_slot_uses(uint8_t id);

/** @brief Bit i set: the device in slot i uses device `id`; REMOVE of it is refused while any. */
uint64_t sys_device_slot_used_by(uint8_t id);

#ifdef __cplusplus
}
#endif
