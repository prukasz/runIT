#pragma once

#include <stdint.h>

#include "sys_event.h"

/**
 * @file sys_watch_config.h
 * @brief What a watch is, the one place it is assigned (wire format: never renumber): its trigger, where its entries
 * go, the head ADD takes and the row LIST sends. Included by sys_watch.h; published to the app by
 * templates/generate_enums.py.
 *
 *   entry  [owner u8][id u8][status u8][data]
 *     status router_status_e: OK with the read's answer, ERROR with the error record
 *   to its owner   STREAM frame [SYS_DATA_CONNECTOR_WATCHES][entry], seq: the watch's count of entries
 *   to a connector the entry is the payload
 */

#define SYS_WATCH_TICK_MS 10u  // the unit of a watch's period and silence: the watches' timer runs every tick

#define SYS_WATCH_COPY_MAX 32u  // the longest answer the client keeps as a copy (cached reads); longer ones are asked for

#define SYS_WATCH_READ_MAX 48u  // the longest read a watch keeps: [key][dir][code | id u16 + op u16][args]

/* Where a watch's entries go (sys_watch_head_t.connector): this byte, or the id of a connector of this board. */
//#ref-defines watch_to @alias Watch target @prefix SYS_WATCH_TO_
#define SYS_WATCH_TO_OWNER 0x00u  //@alias Owner @description Straight to the one who added it, in a STREAM frame

/** @brief When a watch runs its read. */
//#ref-enum @alias Watch trigger
typedef enum {
  SYS_WATCH_PERIODIC = 0,   //@alias Every period @description The read every period, its answer sent
  SYS_WATCH_ON_CHANGE = 1,  //@alias On change @description The read every period, sent when its answer changed
                            //  from the last entry sent
  SYS_WATCH_ON_EVENT = 2,   //@alias On event @description The read when a matching event happens, at most every period
} sys_watch_trigger_e;

/** @brief A watch's head, as ADD takes it; its read follows: [key][dir READ][code | id u16 + op u16][args]. */
typedef struct __attribute__((packed)) {
  uint8_t id;                 /**< 1..255, its owner's choice: ADD of an id its owner has replaces that watch */
  uint8_t connector;          /**< Where its entries go: SYS_WATCH_TO_OWNER, or a connector of this board */
  uint8_t trigger;            /**< sys_watch_trigger_e */
  uint16_t period;            /**< Ticks (SYS_WATCH_TICK_MS). PERIODIC: the period; ON_CHANGE: how often it reads;
                                   ON_EVENT: the shortest gap */
  uint16_t max_silence;       /**< Ticks. ON_CHANGE: sent anyway after this, so silence never looks dead; 0: never */
  sys_event_filter_t filter;  /**< ON_EVENT; else zero */
} sys_watch_head_t;

_Static_assert(sizeof(sys_watch_head_t) == 12, "watch head");

/** @brief A watch as LIST sends it: its head, its owner, how long its read is (the read itself is not sent). */
typedef struct __attribute__((packed)) {
  sys_watch_head_t head;
  uint8_t owner;
  uint8_t read_len;
} sys_watch_info_t;

_Static_assert(sizeof(sys_watch_info_t) == 14, "watch info");
