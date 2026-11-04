#pragma once

/**
 * @file sys_remote_config.h
 * @brief What a remote device's frame carries, the one place it is assigned (wire format: never renumber). Included by
 * sys_remote.h; published to the app by templates/generate_enums.py.
 *
 *   STREAM frame  [SYS_SESSION_CHANNEL_REMOTE][id u16][kind u8][data]   to each subscriber of device `id`
 *     seq   the subscription's count of frames: a gap is frames lost
 *     data  EVENT: sys_event_t (its source the device's id on its own board); ERROR: an error record (ERRORS.MD)
 */

/** @brief What a remote device's frame carries. */
//#ref-enum @alias Remote frame
typedef enum {
  SYS_REMOTE_EVENT = 0,  //@alias Event @description An event of the device: sys_event_t
  SYS_REMOTE_ERROR = 1,  //@alias Error @description An error of the device outside a command: an error record
} sys_remote_kind_e;
