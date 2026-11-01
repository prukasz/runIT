#pragma once

#include <stdbool.h>


/**
 * @file sys_watch.h (draft, step 4)
 * @brief What sys_watch.h gains: the client of the watches this board added on other boards, which hands each entry
 * that comes back to the one who asked for it, and answers this board's reads of other boards from them (Cached
 * reads). Not built.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An entry of a watch this board added on another board, on the RX task it came on: copy what you keep and
 * return; never block.
 * @param ctx     From sys_watch_remote_add().
 * @param board   The board that runs the watch.
 * @param status  router_status_e: OK with the read's answer, ERROR with the error record (also when the ADD failed or
 *                the board is gone: then `data` is this board's record of it).
 * @param data    The answer or the record, valid for the call only.
 * @param len     Bytes in `data`.
 * @param lost    Entries of this watch lost since the one before (the gap in its seq).
 */
typedef void (*sys_watch_entry_f)(void* ctx, uint8_t board, uint8_t status, const uint8_t* data, uint32_t len,
                                  uint16_t lost);

/**
 * @brief Adds a watch on another board (SYS_WATCH.MD "Client"): the client gives it an id, sends ADD and hands each
 * entry to `cb`; it sends the ADD again when that board's session opens again. Never blocks, any task.
 * @param board  The board that runs it.
 * @param head   The watch; its `id` is set by the client, `connector` must be SYS_WATCH_TO_OWNER.
 * @param read   Its read on that board: [key][dir READ][...] (copied).
 * @param cb     Where its entries go.
 * @param ctx    Given to `cb`.
 * @param id     Out: the id the client gave it.
 * @return NULL when sent; ERR_WATCH_FULL (CONFIG_SYS_WATCH_CLIENT_MAX), ERR_WATCH_BAD_ARG, or the call's error.
 */
MUST_USE err_h sys_watch_remote_add(uint8_t board, const sys_watch_head_t* head, cmd_in_t read, sys_watch_entry_f cb,
                                    void* ctx, uint8_t* id);

/**
 * @brief Removes a watch this board added on another board: sends REMOVE and forgets it.
 * @param board  The board.
 * @param id     From sys_watch_remote_add().
 * @return NULL when removed here; ERR_WATCH_NOT_FOUND.
 */
MUST_USE err_h sys_watch_remote_remove(uint8_t board, uint8_t id);

/**
 * @brief Reads on another board through the client (SYS_WATCH.MD "Cached reads"): a fresh copy from a watch of the
 * same request when there is one, else (`wait`) the answer of a request now; the client makes the watch when the
 * request is read again and removes it when no one reads it. Not inside a device op unless the caller may wait there.
 * @param board       The board.
 * @param read        The read: [key][dir READ][...].
 * @param max_age_ms  The oldest copy it takes; 0: always asks.
 * @param wait        true: ask now when there is no fresh copy (sys_session_call_wait); false: never wait.
 * @param buf         Out: the value.
 * @param cap         Bytes `buf` holds.
 * @param len         Out: the value's bytes.
 * @return NULL with the value; ERR_WATCH_NO_VALUE, ERR_WATCH_STALE (no wait), the answer's error (ERR_REMOTE over that
 * board's chain), or what sys_session_call_wait returns.
 */
MUST_USE err_h sys_watch_read(uint8_t board, cmd_in_t read, uint32_t max_age_ms, bool wait, void* buf, uint32_t cap,
                              uint32_t* len);

/**
 * @brief A write on another board was answered: the copies of reads of the same thing there are old until a read
 * after it came (SYS_WATCH.MD "Cached reads"). Never blocks, any task.
 * @param board       The board.
 * @param write       The write: [key][dir WRITE][target][...].
 * @param target_len  Bytes after the dir byte that name the thing (the device key: its id, 2).
 */
void sys_watch_stale(uint8_t board, cmd_in_t write, uint32_t target_len);

#ifdef __cplusplus
}
#endif
