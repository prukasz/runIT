#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_states.h"

/**
 * @brief Class byte for sys_actions' own control packets (record/stop/remove/bind).
 *
 * Owned here (not by the codec header) because it is sys_actions' own
 * control-plane protocol, 1:1 with this component - see dec_sys_actions.h in
 * `codecs`, which just maps this class's packet bytes onto the calls below.
 */
#define SYS_ACTIONS_CLASS_HEADER 0x03

/** @brief Default packet-buffer capacity for an action auto-created by record_start()/append_packet(). */
#define SYS_ACTIONS_DEFAULT_MAX_SIZE 512

/** @brief Max actions bound to a single sys_state_e (see sys_actions_bind_state()). */
#define SYS_ACTIONS_MAX_BOUND_PER_STATE 4

/**
 * @brief Initialize sys_actions: registers its control-packet class with
 * sys_interface, installs the recording tap, loads the state->action binding
 * map from NVS, and - if action id 0 has anything stored - loads and invokes
 * it as the "boot action" (see the Overview in SYS_ACTIONS.MD).
 *
 * Must be called after sys_interface_init() and before
 * sys_interface_bind_ble_rx() - class registration is boot-only, not
 * concurrency-safe against a running RX pump (see SYS_INTERFACE.MD).
 */
err_h sys_actions_init(void);

/**
 * @brief Create an action explicitly (the "system call" path, as opposed to
 * auto-creation via sys_actions_record_start() / sys_actions_append_packet()).
 *
 * Only allocates the in-RAM staging buffer packets are appended into - nothing
 * is written to NVS until sys_actions_persist_save() (or sys_actions_record_stop(),
 * which calls it automatically).
 *
 * @param action_id Caller-chosen id (the wire protocol carries this as a single byte).
 * @param max_size Fixed capacity in bytes for the staging buffer (must be > 0).
 * @return err_h NULL on success, including when action_id already has a staging
 *               buffer (left untouched); ERR_INVALID_VAL_UI32 if max_size == 0;
 *               or ERR_BASE_NO_MEM.
 */
err_h sys_actions_create(uint8_t action_id, size_t max_size);

/** @brief Free action_id's in-RAM staging buffer and erase its NVS entry. No-op (returns NULL) if it doesn't exist in either place. */
err_h sys_actions_remove(uint8_t action_id);

/**
 * @brief Remove every action: frees all in-RAM staging buffers and erases
 * every action blob in NVS. The state binding map ("bindmap") is left alone -
 * a binding to a now-missing action id simply fails with ERR_ACTION_NOT_FOUND
 * when invoked, rather than being silently dropped.
 */
err_h sys_actions_remove_all(void);

/**
 * @brief Append one raw frame (class byte included, exactly as it would arrive
 * over the wire) to action_id's in-RAM staging buffer.
 *
 * Auto-creates action_id (SYS_ACTIONS_DEFAULT_MAX_SIZE capacity) if it doesn't
 * exist yet. This is the shared primitive behind both the "seed via system
 * call" path and the live recording tap's own appends. Does **not** persist to
 * NVS by itself - call sys_actions_persist_save() when done appending.
 *
 * @return err_h NULL on success, ERR_ACTION_BLOCK_FULL if the frame doesn't fit
 *               in the remaining staging capacity, ERR_INVALID_VAL_UI32 if len
 *               is 0 or exceeds UINT16_MAX, or ERR_BASE_NO_MEM.
 */
err_h sys_actions_append_packet(uint8_t action_id, const uint8_t* frame, size_t len);

/**
 * @brief Start recording: every subsequent frame observed by sys_interface (of
 * any class except SYS_ACTIONS_CLASS_HEADER itself) is appended to action_id's
 * staging buffer.
 *
 * Auto-creates action_id (SYS_ACTIONS_DEFAULT_MAX_SIZE capacity) if it doesn't
 * exist yet. Multiple actions may record concurrently.
 */
err_h sys_actions_record_start(uint8_t action_id);

/**
 * @brief Stop recording for action_id and persist its staging buffer to NVS
 * (equivalent to calling sys_actions_persist_save() immediately after).
 *
 * No-op (returns NULL) if action_id wasn't recording or doesn't exist.
 */
err_h sys_actions_record_stop(uint8_t action_id);

/**
 * @brief Replay every packet stored under action_id **in NVS** (not the in-RAM
 * staging buffer, if one happens to exist unsaved), in recorded order, through
 * sys_interface_decode() - each stored frame is fed back exactly as if it had
 * just arrived over the wire.
 *
 * sys_interface's RX pump is suspended for the duration
 * (sys_interface_suspend_rx()/_resume_rx()) so live traffic cannot interleave
 * with the replay - see the caveat on that suspend being best-effort, not a
 * hard barrier, in sys_interface.h.
 *
 * @return err_h NULL if every stored frame decoded without error,
 *               ERR_ACTION_NOT_FOUND if action_id has nothing saved in NVS, or
 *               the first failing frame's error chain (replay stops there; the
 *               RX pump is resumed regardless of outcome).
 */
err_h sys_actions_invoke(uint8_t action_id);

/**
 * @brief Bind action_id to state, so sys_states_enter(state) invokes it (via
 * sys_actions_invoke_for_state()) after that state's own base action runs.
 *
 * The whole binding map is persisted to NVS on every call, so bindings survive
 * reboot. A no-op (returns NULL) if action_id is already bound to state.
 *
 * @return err_h NULL on success, ERR_ACTION_STATE_BINDINGS_FULL if state
 *               already has SYS_ACTIONS_MAX_BOUND_PER_STATE actions bound.
 */
err_h sys_actions_bind_state(uint8_t action_id, sys_state_e state);

/**
 * @brief Invoke every action bound to state, in bind order, stopping at the
 * first failure (same convention as sys_device's *_all() sweeps).
 *
 * Called by sys_states_enter() after its own action succeeds; a no-op
 * (returns NULL) if nothing is bound to state.
 */
err_h sys_actions_invoke_for_state(sys_state_e state);

/** @brief Write action_id's in-RAM staging buffer to NVS. ERR_ACTION_NOT_FOUND if action_id has no staging buffer. */
err_h sys_actions_persist_save(uint8_t action_id);

/**
 * @brief Load action_id's blob back from NVS into a (freshly allocated, if
 * needed) in-RAM staging buffer, so it can be inspected or appended to further.
 *
 * @return err_h NULL on success, ERR_ACTION_NOT_FOUND if action_id has nothing saved in NVS.
 */
err_h sys_actions_persist_load(uint8_t action_id);
