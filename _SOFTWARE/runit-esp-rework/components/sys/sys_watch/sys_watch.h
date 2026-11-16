#pragma once

#include <stdint.h>

#include "cmd.h"
#include "sys_session.h"
#include "sys_watch_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file sys_watch.h
 * @brief Watches (SYS_WATCH.MD): reads run on this board every period, their answers (all, or the changed ones)
 * pushed to the peer that added them (ADD, REMOVE, LIST of sys_watch_router), and the cached reads of other boards
 * made of them (sys_watch_read / sys_watch_stale).
 */

/* ----------------------------------------------------------------------------
 * Wire format (never renumber): the ADD head, the LIST row, the triggers. Entries travel on the module's channel
 * SYS_WATCH_CHANNEL (the data matrix's tag) as [id u8][status u8][data], to the watch's owner.
 * ------------------------------------------------------------------------- */

#define SYS_WATCH_CHANNEL SYS_SESSION_CHANNEL(MODULE_ID_WATCH)
_Static_assert(MODULE_ID_WATCH < SYS_SESSION_CHANNEL_FIRST, "a module with a channel has a number under 0x80");

#define SYS_WATCH_TICK_MS  10u  /**< Unit of period and max_silence, and server timer step (ms). */
#define SYS_WATCH_COPY_MAX 32u  /**< Largest OK read answer cached by the client (bytes). */
#define SYS_WATCH_READ_MAX 48u  /**< Largest watched read command (bytes), including router key and dir. */

/** @brief When a watch runs its read and when its entry goes out. */
//#ref-enum @alias Watch Trigger
typedef enum {
  SYS_WATCH_PERIODIC = 0,   //@alias Periodic @description Every period: run read and send answer
  SYS_WATCH_ON_CHANGE = 1,  //@alias On change @description Every period: run read, send when CRC-32 differs from last sent or max_silence elapsed
} sys_watch_trigger_e;

/**
 * @brief ADD head: followed in OP_ADD by the read command (2..SYS_WATCH_READ_MAX bytes, dir == CMD_DIR_READ).
 * Packed, little-endian (consumed in place by CMD_HANDLER_TAIL).
 */
typedef struct __attribute__((packed)) {
  uint8_t id;            /**< Owner's watch id (1..255); ADD with an existing id replaces it */
  uint8_t trigger;       /**< sys_watch_trigger_e */
  uint16_t period;       /**< Ticks of SYS_WATCH_TICK_MS (>= 1) */
  uint16_t max_silence;  /**< Ticks (ON_CHANGE: send anyway after this many silent ticks; 0 = never) */
} sys_watch_head_t;

_Static_assert(sizeof(sys_watch_head_t) == 6, "sys_watch_head_t size");
_Static_assert(_Alignof(sys_watch_head_t) == 1, "sys_watch_head_t must be packed");

/** @brief LIST answer row: one per watch on this board. */
typedef struct __attribute__((packed)) {
  sys_watch_head_t head;
  uint8_t owner;     /**< Peer id receiving the watch's entries */
  uint8_t read_len;  /**< Length of the read command (the command bytes are not sent) */
} sys_watch_info_t;

_Static_assert(sizeof(sys_watch_info_t) == 8, "sys_watch_info_t size");

/**
 * @brief Starts the watches of this board and its cached reads. Boot, after router, sys_event and sys_session.
 * @return NULL on success, else the error of a session listener's registration.
 */
MUST_USE err_h sys_watch_init(void);

/* ----------------------------------------------------------------------------
 * Cached reads of another board (never block on the link)
 * ------------------------------------------------------------------------- */

/**
 * @brief Reads a remote value from the local watch cache (SYS_WATCH.MD "Cached reads"). Never blocks on the wire:
 * - First call for `(board, read)` claims a client slot and asynchronously sends WATCH ADD (ON_CHANGE, period
 *   CONFIG_SYS_WATCH_READ_PERIOD_MS, max_silence 2x), returning ERR_WATCH_NO_VALUE.
 * - While ADD is awaiting its OK answer (the first one, or the one sys_watch_stale() sends), or before the first
 *   PUSH entry after ADD OK arrives, returns ERR_WATCH_NO_VALUE.
 * - If the latest cached entry is older than 3 * CONFIG_SYS_WATCH_READ_PERIOD_MS, returns ERR_WATCH_STALE.
 * - If the latest entry was an error record from `board`, returns that chain wrapped in ERR_REMOTE{board, 0}.
 * - A read not read for CONFIG_SYS_WATCH_IDLE_MS is REMOVEd from `board` and its slot freed.
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
 * @brief Drops what every cached read on `board` holds whose command shares `write.data[0]` (router key) and
 * `write.data[2 .. 2 + target_len - 1]`, and adds its watch again: the entries taken after that ADD's OK were read
 * after the write, and the first comes at once. Call when a write's answer arrives from `board`, so reads do not
 * serve values from before it.
 * @param[in] board      Remote board id.
 * @param[in] write      The completed write command.
 * @param[in] target_len Bytes after `dir` identifying the target (e.g. 2 for device id, 0 to match key only).
 */
void sys_watch_stale(uint8_t board, cmd_in_t write, uint32_t target_len);

#ifdef __cplusplus
}
#endif
