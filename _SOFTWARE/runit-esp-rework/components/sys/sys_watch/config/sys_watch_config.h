#pragma once

#include <stdint.h>

#include "sys_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file sys_watch_config.h
 * @brief Watch wire formats and limits (SYS_WATCH.MD): the ADD head, the LIST row, and the triggers.
 *
 * Entries travel on session PUSH channel SYS_SESSION_CHANNEL_WATCHES (0x80) as:
 *   [0x80][id u8][status u8][data]
 * There is no `owner` byte (the PUSH frame's dst peer is the watch's owner).
 */

#define SYS_WATCH_TICK_MS  10u  /**< Unit of period and max_silence, and server timer step (ms). */
#define SYS_WATCH_COPY_MAX 32u  /**< Largest OK read answer cached by the client (bytes). */
#define SYS_WATCH_READ_MAX 48u  /**< Largest watched read command (bytes), including router key and dir. */

/** @brief When a watch runs its read and when its entry goes out. */
//#ref-enum @alias Watch Trigger
typedef enum {
  SYS_WATCH_PERIODIC = 0,   /**< Every period: run read, send answer */
  SYS_WATCH_ON_CHANGE = 1,  /**< Every period: run read, send when CRC-32 differs from last entry sent or max_silence elapsed */
  SYS_WATCH_ON_EVENT = 2,   /**< When a matching event occurs, at most once per period */
} sys_watch_trigger_e;

/**
 * @brief ADD head: followed in OP_ADD by the read command (2..SYS_WATCH_READ_MAX bytes, dir == CMD_DIR_READ).
 * Packed, little-endian (consumed in place by CMD_HANDLER_TAIL).
 */
typedef struct __attribute__((packed)) {
  uint8_t id;             /**< Owner's watch id (1..255); ADD with an existing id replaces it */
  uint8_t trigger;        /**< sys_watch_trigger_e */
  uint16_t period;        /**< Ticks of SYS_WATCH_TICK_MS (>= 1) */
  uint16_t max_silence;   /**< Ticks (ON_CHANGE: send anyway after this many silent ticks; 0 = never) */
  sys_event_key_t filter; /**< Event filter for ON_EVENT (0 otherwise) */
} sys_watch_head_t;

_Static_assert(sizeof(sys_watch_head_t) == 14, "sys_watch_head_t size");
_Static_assert(_Alignof(sys_watch_head_t) == 1, "sys_watch_head_t must be packed");

/** @brief LIST answer row: one per watch on this board. */
typedef struct __attribute__((packed)) {
  sys_watch_head_t head;
  uint8_t owner;     /**< Peer id receiving the watch's entries */
  uint8_t read_len;  /**< Length of the read command (the command bytes are not sent) */
} sys_watch_info_t;

_Static_assert(sizeof(sys_watch_info_t) == 16, "sys_watch_info_t size");

#ifdef __cplusplus
}
#endif
