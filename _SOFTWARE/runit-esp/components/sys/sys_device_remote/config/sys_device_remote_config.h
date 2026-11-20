#pragma once

/**
 * @file sys_device_remote_config.h
 * @brief What a remote device's frame carries, the one place it is assigned (wire format: never renumber). Included by
 * sys_device_remote.h; published to the app by templates/generate_enums.py.
 *
 *   A frame out of the port REMOTE (after its header)               [id u8][kind u8][data]   to each subscriber of device `id`
 *     data  EVENT: sys_event_t with its payload (its source the device's id on its own board); ERROR: an error record
 */

/** @brief What a remote device's frame carries. */
//#ref-enum @alias Remote frame
typedef enum {
  SYS_DEVICE_REMOTE_EVENT = 0,  //@alias Event @description An event of the device: sys_event_t
  SYS_DEVICE_REMOTE_ERROR = 1,  //@alias Error @description An error of the device outside a command: an error record
} sys_device_remote_kind_e;
