#pragma once

#include <stdint.h>

#include "cmd.h"
#include "errors.h"

/**
 * @file sys_device_remote.h
 * @brief Remote devices (SYS_DEVICE.MD "Remote devices"): a device of any type that is a device of another board. The
 * registry keeps its type and where it is; sys_device_call hands its calls, without the registry lock, to the hook
 * sys_remote sets (SYS_REMOTE.MD), which carries them to that board.
 * @usage sys_device_remote_t r = {.board = 2, .remote_id = 0x0010, .mode = SYS_DEVICE_REMOTE_ASYNC,
 *                              .max_age_ms = 100};   // CREATE_REMOTE args: [SYS_DEVICE_TYPE_SERVO u16][r]
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How a remote device's calls wait (SYS_DEVICE.MD "Remote devices"). */
//#ref-enum @alias Remote mode
typedef enum {
  SYS_DEVICE_REMOTE_ASYNC = 0,     //@alias Async @description A write returns once taken; a read answers a copy
  SYS_DEVICE_REMOTE_BLOCKING = 1,  //@alias Blocking @description A call waits for its answer; a wired neighbour only
} sys_device_remote_mode_e;

/** @brief Where a remote device is: CREATE_REMOTE's struct, after the type. */
typedef struct __attribute__((packed)) {
  uint8_t board;        /**< The board the device is on */
  uint16_t remote_id;   /**< Its id there */
  uint8_t mode;         /**< sys_device_remote_mode_e */
  uint16_t max_age_ms;  /**< The oldest copy a read may answer; blocking: 0 always asks; async: at least 1 */
} sys_device_remote_t;

_Static_assert(sizeof(sys_device_remote_t) == 6, "remote record");

/** @brief The last board id a remote device may name (SYS_SESSION_ID_LAST). */
#define SYS_DEVICE_REMOTE_BOARD_LAST 0xEFu

/** @brief The longest request (the args of a call, 6 bytes of header more) a remote device's call carries. */
#define SYS_DEVICE_REMOTE_ARGS_MAX 64u

/** @brief The remote hook: what carries a remote device's calls (sys_remote). */
typedef struct {
  /**
   * @brief One call of a remote device, without the registry lock (a caller's own lock may be held).
   * @param where    The device's record.
   * @param req      The call as the device command for that board: [ROUTER_KEY_DEVICE][dir][remote_id u16][op u16][args].
   * @param out      Out: a read's answer.
   * @param cap      Bytes `out` holds.
   * @param out_len  Out: the answer's bytes.
   * @return NULL; ERR_DEV_REMOTE_DOWN, ERR_DEV_REMOTE_NOT_WIRED, ERR_SESSION_BUSY (later), the answer's error.
   */
  err_h (*call)(const sys_device_remote_t* where, cmd_in_t req, uint8_t* out, uint32_t cap, uint32_t* out_len);
  void (*added)(uint8_t board);    /**< A remote device of that board was made; under the registry lock: never block */
  void (*removed)(uint8_t board);  /**< One was removed; the same */
} sys_device_remote_ops_t;

/**
 * @brief Sets the remote hook. Boot only, one (sys_remote_init).
 * @param ops  The hook; valid for good.
 */
void sys_device_set_remote(const sys_device_remote_ops_t* ops);

/**
 * @brief The local id of a remote device. Any task.
 * @param board      Its board.
 * @param remote_id  Its id there.
 * @return The local id; SYS_DEVICE_ID_NONE when there is none.
 */
uint16_t sys_device_find_remote(uint8_t board, uint16_t remote_id);

#ifdef __cplusplus
}
#endif
