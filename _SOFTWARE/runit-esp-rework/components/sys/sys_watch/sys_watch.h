#pragma once

#include <stdint.h>

#include "cmd.h"
#include "sys_watch_config.h"
#include "sys_watch_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file sys_watch.h
 * @brief Watches (SYS_WATCH.MD): reads run on this board by a trigger (period, change, or event) whose answers are
 * pushed to their owner, plus the simplified cached-read client (sys_watch_read / sys_watch_stale).
 */

/**
 * @brief Starts watches on this board (both server and client). Boot step, after router, sys_event and sys_session.
 * @return NULL on success, else error from session listener registration.
 */
MUST_USE err_h sys_watch_init(void);

/* ----------------------------------------------------------------------------
 * Server (watches running on this board)
 * ------------------------------------------------------------------------- */

/**
 * @brief Adds or replaces a watch owned by `owner`. When an existing `(owner, head->id)` watch is replaced, its
 * previous event subscription is cleanly unsubscribed and pending answers from the prior generation are dropped.
 * @param[in] head  Watch configuration (id 1..255, valid trigger, period >= 1).
 * @param[in] read  Command to execute periodically / on event (dir must be CMD_DIR_READ, 2..SYS_WATCH_READ_MAX bytes).
 * @param[in] owner Destination peer id (not SYS_SESSION_ID_SELF, _NONE, _BROADCAST, or this board's own id).
 * @return NULL; ERR_WATCH_NOT_READ, ERR_WATCH_BAD_ARG, ERR_WATCH_FULL, or sys_event_subscribe() error.
 */
MUST_USE err_h sys_watch_add(const sys_watch_head_t* head, cmd_in_t read, uint8_t owner);

/**
 * @brief Removes the watch `(owner, id)`.
 * @return NULL; ERR_WATCH_NOT_FOUND.
 */
MUST_USE err_h sys_watch_remove(uint8_t owner, uint8_t id);

/**
 * @brief Copies up to `max` active watches into `out`.
 * @return Number of entries written.
 */
uint8_t sys_watch_list(sys_watch_info_t* out, uint8_t max);

/* ----------------------------------------------------------------------------
 * Client (cached remote reads; cache-only, never blocks on the link)
 * ------------------------------------------------------------------------- */

/**
 * @brief Reads a remote value from the local watch cache (SYS_WATCH.MD "Cached reads"). Never blocks on the wire:
 * - First call for `(board, read)` claims a client slot and asynchronously sends WATCH ADD (ON_CHANGE, period
 *   CONFIG_SYS_WATCH_READ_PERIOD_MS, max_silence 2x), returning ERR_WATCH_NO_VALUE.
 * - While ADD is awaiting its OK answer, or after sys_watch_stale() until the refresh read finishes, or before the
 *   first PUSH entry after ADD OK arrives, returns ERR_WATCH_NO_VALUE.
 * - If the latest cached entry is older than 3 * CONFIG_SYS_WATCH_READ_PERIOD_MS, returns ERR_WATCH_STALE.
 * - If the latest entry was an error record from `board`, returns that chain wrapped in ERR_REMOTE{board, 0}.
 * - Slots unused for max(30 * CONFIG_SYS_WATCH_READ_PERIOD_MS, CONFIG_SYS_WATCH_IDLE_MIN_MS) are automatically
 *   REMOVEd from `board` and freed.
 *
 * @param[in]  board Remote board id.
 * @param[in]  read  Read command (`[key][CMD_DIR_READ][...]`, 2..SYS_WATCH_READ_MAX bytes).
 * @param[out] buf   Output buffer for cached answer.
 * @param[in]  cap   Capacity of `buf`.
 * @param[out] len   Cached answer length (set before ERR_CMD_BAD_LEN check if `cap` is too small).
 * @return NULL on cached hit; ERR_WATCH_BAD_ARG, ERR_WATCH_NOT_READ, ERR_WATCH_FULL, ERR_WATCH_NO_VALUE,
 *         ERR_WATCH_STALE, ERR_CMD_BAD_LEN, or ERR_REMOTE chain from a failed remote read.
 */
MUST_USE err_h sys_watch_read(uint8_t board, cmd_in_t read, void* buf, uint32_t cap, uint32_t* len);

/**
 * @brief Marks every active cached read on `board` whose command shares `read.data[0]` (router key) and
 * `read.data[2 .. 2 + target_len - 1]` as OLD and starts an immediate direct read refresh. Call when a write answer
 * arrives from `board` so subsequent reads do not serve pre-write values.
 * @param[in] board      Remote board id.
 * @param[in] write      The completed write command.
 * @param[in] target_len Bytes after `dir` identifying the target (e.g. 2 for device id, 0 to match key only).
 */
void sys_watch_stale(uint8_t board, cmd_in_t write, uint32_t target_len);

#ifdef __cplusplus
}
#endif
