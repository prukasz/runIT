#pragma once
#include <sdkconfig.h>
#include <stddef.h>
#include <stdint.h>
#include "sys_error.h"

/**
 * @brief Class byte for sys_actions' own control packets (record/stop/remove).
 *
 * Owned here (not by the codec header) because it is sys_actions' own
 * control-plane protocol, 1:1 with this component - see dec_sys_actions.h in
 * `codecs`, which just maps this class's packet bytes onto the calls below.
 */
#define SYS_ACTIONS_CLASS_HEADER 0x03

/** @brief Valid action_id range is 0..CONFIG_SYS_ACTIONS_ID_SPACE-1 - one unified space for every action. */

/**
 * @brief Number of action ids (0..CONFIG_SYS_ACTIONS_STATIC_SLOTS-1) that may have a
 * hardcoded C function bound via sys_actions_bind_static(). The rest of the
 * CONFIG_SYS_ACTIONS_ID_SPACE range is blob-only, same as any other action.
 */

/**
 * @brief Hardcoded behavior an action can carry in addition to (or instead
 * of) its recorded packet blob - what used to be a sys_states "base action".
 * @param arg Caller-supplied context, passed through from sys_actions_bind_static().
 */
typedef err_h (*action_static_func_t)(void* arg);

/**
 * @brief Initialize sys_actions: registers its control-packet class with
 * sys_interface, enables the recording tap and starts its polling task,
 * registers the built-in static functions on action ids 1-5 (freeze/resume/
 * suspend/reset/hard_reset, moved from the removed sys_states component), and
 * - if action id 0 has anything stored - invokes it as the "boot action" (see
 * the Overview in SYS_ACTIONS.MD).
 *
 * Must be called after sys_interface_init() and before
 * sys_interface_bind_ble_rx() - class registration is boot-only, not
 * concurrency-safe against a running RX receiver (see SYS_INTERFACE.MD).
 */
err_h sys_actions_init(void);

/**
 * @brief Bind a hardcoded C function to action_id, run by sys_actions_invoke()
 * before that action's recorded blob (if any). Overwrites any function
 * already bound to action_id.
 *
 * @param action_id Must be < SYS_ACTIONS_STATIC_SLOTS.
 * @param fn Function to run; passing NULL clears the binding.
 * @param arg Opaque context passed to fn on every invoke.
 * @return err_h NULL on success, ERR_INVALID_VAL_UI32 if action_id is out of range.
 */
err_h sys_actions_bind_static(uint8_t action_id, action_static_func_t fn, void* arg);

/** @brief Erase action_id's stored blob from NVS. No-op (returns NULL) if nothing is stored under it. */
err_h sys_actions_remove(uint8_t action_id);

/**
 * @brief Remove every action: erases every action blob in the "sys_actions"
 * NVS namespace. Does not affect a recording currently in progress (if any) -
 * see sys_actions_record_stop().
 */
err_h sys_actions_remove_all(void);

/**
 * @brief Append one raw frame (class byte included, exactly as it would arrive
 * over the wire) to action_id's stored blob.
 *
 * One atomic load-modify-save against NVS: reads whatever is currently under
 * action_id (starting from empty if nothing is stored yet), appends the
 * frame, and writes the result straight back - there is no separate persist
 * step, every call is durable the moment it returns.
 *
 * @return err_h NULL on success, ERR_INVALID_VAL_UI32 if action_id is out of
 *               range or len is 0 or exceeds UINT16_MAX, or ERR_ESP_ERR.
 */
err_h sys_actions_append_packet(uint8_t action_id, const uint8_t* frame, size_t len);

/**
 * @brief Start recording: every subsequent frame observed by sys_interface (of
 * any class except SYS_ACTIONS_CLASS_HEADER itself) is appended to action_id's
 * blob, in RAM, until sys_actions_record_stop() persists it.
 *
 * Only one action may record at a time - starts from whatever's already
 * stored under action_id in NVS (empty if none).
 *
 * @return err_h NULL on success, ERR_INVALID_VAL_UI32 if action_id is out of
 *               range, ERR_ACTION_RECORDING_BUSY if a different action is
 *               already recording.
 */
err_h sys_actions_record_start(uint8_t action_id);

/**
 * @brief Stop recording and persist the accumulated blob to NVS under action_id.
 *
 * No-op (returns NULL) if action_id isn't the action currently recording.
 */
err_h sys_actions_record_stop(uint8_t action_id);

/**
 * @brief Run action_id's bound static function (if any) then replay every
 * packet stored under action_id in NVS (if any), in recorded order, through
 * sys_interface_decode() - each stored frame is fed back exactly as if it had
 * just arrived over the wire.
 *
 * sys_interface's RX receiver is suspended for the duration of blob replay
 * (sys_interface_suspend_rx()/_resume_rx()) so live traffic cannot interleave
 * with it - see the caveat on that suspend being best-effort, not a hard
 * barrier, in sys_interface.h.
 *
 * @return err_h NULL if the static function (if any) and every stored frame
 *               succeeded, ERR_INVALID_VAL_UI32 if action_id is out of range,
 *               ERR_ACTION_NOT_FOUND if action_id has neither a bound static
 *               function nor anything stored in NVS, or the first failure's
 *               error chain (stops at the first failure, static function or
 *               frame; the RX receiver is resumed regardless of outcome).
 */
err_h sys_actions_invoke(uint8_t action_id);
