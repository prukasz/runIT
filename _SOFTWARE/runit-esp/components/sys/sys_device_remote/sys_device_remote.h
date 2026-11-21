#pragma once

#include "cmd.h"
#include "errors.h"
#include "sys_device_remote_config.h"
#include "sys_device_remote_errors.h"

/* The module's port REMOTE (DATA_MATRIX_PORT(MODULE, MODULE_ID_REMOTE)): a device's events and errors to its
   subscribers' sessions, [hdr PUSH, MODULE_ID_REMOTE][id u8][kind u8][data]; the same from the boards whose devices this
   board has. */

/**
 * @file sys_device_remote.h
 * @brief Devices of other boards (SYS_DEVICE_REMOTE.MD): sets sys_device's remote hook, which sends a remote device's calls
 * to its board as that board's device command, and keeps the subscriptions to devices: the other boards' to this
 * board's devices (their events and errors sent out of the module's port REMOTE), and this board's
 * to its remote devices (what comes is published and reported as the local id's). Nothing depends on it.
 * @usage ERR_TRY(sys_device_remote_init());   // boot, after the session, the watches and the events
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sets the remote hook of sys_device, follows the registry's remote devices (a CREATED one subscribes on its
 * board), listens to the session (gone and reached peers) and registers REMOTE (its task starts by itself). Boot step, after
 * sys_session, sys_watch and sys_event.
 * @return NULL when started; the session's or the event bus's error.
 */
MUST_USE err_h sys_device_remote_init(void);

/**
 * @brief Subscribes a peer to a device of this board: its events and errors outside a command go to the peer (SUBSCRIBE).
 * Again for the same peer and device: one subscription. Until UNSUBSCRIBE or the peer is gone.
 * @param[in] id   The device (whether it exists is not checked: an id with no device sends nothing).
 * @param[in] peer The peer (SUBSCRIBE: the command's src).
 * @return NULL when subscribed; ERR_BAD_ARG (CMD_SRC_SELF, NONE or ALL), ERR_SESSION_NO_ROUTE (the peer's session has
 * no sub-port REMOTE), ERR_REMOTE_SUBS_FULL, or the event subscription's error.
 */
MUST_USE err_h sys_device_remote_subscribe(uint8_t id, uint8_t peer);

/**
 * @brief Ends a peer's subscription to a device.
 * @param[in] id   The device.
 * @param[in] peer The peer.
 */
void sys_device_remote_unsubscribe(uint8_t id, uint8_t peer);

/**
 * @brief An error reported on this board (the app's error sink): one outside a command (`cmd` NULL) whose chain names a
 * device with subscribers (ERR_DEV_ID) is queued for them. Never blocks, any task.
 * @param[in] chain The reported chain (borrowed).
 * @param[in] cmd   The command it came from, or NULL.
 */
void sys_device_remote_on_error(err_h chain, const cmd_req_t* cmd);

#ifdef __cplusplus
}
#endif
