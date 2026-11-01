#pragma once

#include "cmd.h"
#include "errors.h"
#include "sys_watch_config.h"
#include "sys_watch_errors.h"

/**
 * @file sys_watch.h
 * @brief Watches (SYS_WATCH.MD): a read run on this board by a trigger (a period, a change of its answer, an event),
 * its answer sent as an entry straight to its owner (who added it), or to a connector (the board's own watches). The
 * read is a command of the board itself (router_inject), so a watch needs no task of its own; an owner's entries
 * leave on the router's work task, after the answers of the commands that ran before their read. A watch is its
 * owner's: its id is the owner's, and it ends when the owner is gone.
 * @usage ERR_TRY(sys_watch_add(&head, (cmd_in_t){read, read_len}, SYS_SESSION_ID_SELF));
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts the watches: their timer and the gone-peer listener (sys_session_on_gone). Boot step, after the
 * session and the connectors.
 * @return NULL when started; the timer's or the session's error.
 */
MUST_USE err_h sys_watch_init(void);

/**
 * @brief Adds a watch, or replaces the owner's watch with its id.
 * @param head   The watch.
 * @param read   Its read: [key][dir READ][...] (copied).
 * @param owner  Who it is for: a peer's id (ADD: the command's src), or SYS_SESSION_ID_SELF (the board config's
 *               standing ones, which never end).
 * @return NULL when added; ERR_WATCH_NOT_READ (the request is no read), ERR_WATCH_BAD_ARG (id 0, a period of 0, an
 * unknown trigger, a read longer than SYS_WATCH_READ_MAX, SYS_WATCH_TO_OWNER for SYS_SESSION_ID_SELF), ERR_WATCH_FULL
 * (CONFIG_SYS_WATCH_MAX), or the event subscription's error.
 */
MUST_USE err_h sys_watch_add(const sys_watch_head_t* head, cmd_in_t read, uint8_t owner);

/**
 * @brief Removes a watch of an owner.
 * @param owner  Its owner.
 * @param id     The owner's id of it.
 * @return NULL when removed; ERR_WATCH_NOT_FOUND.
 */
MUST_USE err_h sys_watch_remove(uint8_t owner, uint8_t id);

/**
 * @brief The watches, for LIST.
 * @param info  Out: one row per watch.
 * @param max   Rows `info` holds.
 * @return Rows written.
 */
uint8_t sys_watch_list(sys_watch_info_t* info, uint8_t max);

#ifdef __cplusplus
}
#endif
