#pragma once

#include "cmd.h"
#include "errors.h"
#include "sys_remote_config.h"
#include "sys_remote_errors.h"

/**
 * @file sys_remote.h
 * @brief Devices of other boards (SYS_REMOTE.MD): sets sys_device's remote hook, which sends a remote device's calls
 * to its board as that board's device command, and keeps the subscriptions to devices: the other boards' to this
 * board's devices (their events and errors sent in the session's channel SYS_SESSION_CHANNEL_REMOTE), and this board's
 * to its remote devices (what comes is published and reported as the local id's). Nothing depends on it: a board
 * without other boards leaves it out.
 * @usage ERR_TRY(sys_remote_init());   // boot, after the session, the watches and the events
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sets the remote hook of sys_device, listens to the session (gone and reached peers, the remote channel) and
 * starts its task. Boot step, after sys_session, sys_watch and sys_event.
 * @return NULL when started; the session's error.
 */
MUST_USE err_h sys_remote_init(void);

/**
 * @brief Subscribes a peer to a device of this board: its events and errors outside a command go to the peer (SUBSCRIBE).
 * Again for the same peer and device: one subscription, its count kept. Until UNSUBSCRIBE or the peer is gone.
 * @param id    The device (whether it exists is not checked: an id with no device sends nothing).
 * @param peer  The peer (SUBSCRIBE: the command's src).
 * @return NULL when subscribed; ERR_CMD_BAD_ARG (SYS_SESSION_ID_SELF, NONE or BROADCAST), ERR_REMOTE_SUBS_FULL, or
 * the event subscription's error.
 */
MUST_USE err_h sys_remote_subscribe(uint16_t id, uint8_t peer);

/**
 * @brief Ends a peer's subscription to a device.
 * @param id    The device.
 * @param peer  The peer.
 */
void sys_remote_unsubscribe(uint16_t id, uint8_t peer);

/**
 * @brief An error reported on this board (runit's error sink): one outside a command (`cmd` NULL) whose chain names a
 * device with subscribers (ERR_DEV_ID) is queued for them. Never blocks, any task.
 * @param chain  The reported chain (borrowed).
 * @param cmd    The command it came from, or NULL.
 */
void sys_remote_on_error(err_h chain, const cmd_req_t* cmd);

#ifdef __cplusplus
}
#endif
