#pragma once
#include <sdkconfig.h>
#include <stddef.h>
#include <stdint.h>
#include "sys_error.h"

/**
 * @brief Class byte for sys_actions' own control packets.
 *
 * Owned here (not by the codec header) because it is sys_actions' own
 * control-plane protocol, 1:1 with this component - see dec_sys_actions.h in
 * `codecs`, which just maps this class's packet bytes onto the calls below.
 */
#define SYS_ACTIONS_CLASS_HEADER 0x03

/**
 * @brief Action scopes:
 * - 0x00: Static action (compile-time function pointer table)
 * - 0x01: Dynamic action (runtime/NVS recorded frame sequence, max 2 KB)
 */
#define SYS_ACTION_SCOPE_STATIC  0x00
#define SYS_ACTION_SCOPE_DYNAMIC 0x01

/** @brief Reserved sentinel; not a valid action ID in either scope. */
#define SYS_ACTION_ID_NONE 0

/** @brief Maximum blob size per dynamic action (2 KB). */
#define SYS_ACTIONS_MAX_BLOB_SIZE 2048

/**
 * @brief Hardcoded behavior a static action carries. Takes no arguments.
 */
typedef err_h (*action_static_func_t)(void);

/**
 * @brief Initialize sys_actions: registers its control-packet class with
 * sys_interface, starts the recording tap polling task, and
 * registers the built-in static functions on action ids 2-6 (freeze/resume/
 * suspend/reset/hard_reset).
 *
 * Must be called after sys_interface_init() and before
 * sys_interface_register_rx_source() - class registration is boot-only, not
 * concurrency-safe against a running RX receiver (see SYS_INTERFACE.MD).
 */
err_h sys_actions_init(void);

/**
 * @brief Bind a hardcoded C function to a static action_id.
 *
 * @param action_id Must be >= 1 and < CONFIG_SYS_ACTIONS_STATIC_SLOTS.
 * @param fn Function to run; passing NULL clears the binding.
 * @return err_h NULL on success, ERR_INVALID_VAL_UI32 if action_id is out of range.
 */
err_h sys_actions_bind_static(uint8_t action_id, action_static_func_t fn);

/**
 * @brief Invoke an action by scope and id.
 *
 * - SYS_ACTION_SCOPE_STATIC (0x00): invokes bound static function `id`.
 *   Returns ERR_ACTION_NOT_FOUND if id is unbound or >= CONFIG_SYS_ACTIONS_STATIC_SLOTS.
 * - SYS_ACTION_SCOPE_DYNAMIC (0x01): loads recorded blob `id` (1..255) from NVS,
 *   suspends RX, replays frames through sys_interface_decode(), resumes RX.
 *   Returns ERR_ACTION_NOT_FOUND if nothing is stored in NVS under `id`.
 *
 * @param scope Action scope (0x00 = static, 0x01 = dynamic).
 * @param id Action ID.
 * @return err_h NULL on success, or the error chain of the failure.
 */
err_h sys_actions_invoke(uint8_t scope, uint8_t id);

/**
 * @brief Erase a dynamic action's stored blob from NVS.
 * No-op (returns NULL) if nothing is stored under it.
 *
 * @param id Action ID (1..255).
 */
err_h sys_action_remove(uint8_t id);

/**
 * @brief Remove every dynamic action: erases every action blob in the "sys_actions"
 * NVS namespace. Does not affect a recording currently in progress in RAM.
 */
err_h sys_action_remove_all(void);

/**
 * @brief Start recording: subsequent frames observed by sys_interface (of
 * any class except SYS_ACTIONS_CLASS_HEADER itself) are appended to id's
 * blob in RAM, until sys_action_record_stop() persists it.
 *
 * Only one action may record at a time. The action ID is remembered internally.
 *
 * @param id Dynamic action ID (1..255).
 * @return err_h NULL on success, ERR_ACTION_RECORDING_BUSY if already recording.
 */
err_h sys_action_record_start(uint8_t id);

/**
 * @brief Stop capture, drain all queued frames into the active recording,
 * and persist the accumulated blob under the remembered action ID.
 * Action record_stop is not captured in the action.
 *
 * No-op (returns NULL) if no action is currently recording.
 */
err_h sys_action_record_stop(void);

/**
 * @brief Weak domain error hook for Actions faults.
 */
extern err_h sys_actions_report_fault(err_h node, err_h chain) __attribute__((weak));
