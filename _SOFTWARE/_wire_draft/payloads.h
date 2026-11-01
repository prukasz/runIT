#pragma once

#include <stdint.h>

/* DRAFT: what the sessions (sys_session/) and the remote devices (sys_device/) need of modules outside _wire_draft:
 * the watches' root key and error module, the remote devices, the event flag and the remote events, the board as an
 * error source, the VM. The error record (err_cmd_t, err_record_cmd_t, err_record_parse) is in errors.h (ERRORS.MD).
 * Not built. Each section names the module it goes to. */

/* ----------------------------------------------------------------------------
 * router_ids.h, err_modules.h: the watches' root key (after ROUTER_KEY_SESSION 0x0E) and error module (after
 * ERR_MOD_SYS_SESSION 18).
 *
 *   ROUTER_KEY_WATCH    0x0F   sys_watch_router              ERR_MOD_SYS_WATCH         19
 * CONFIG_ROUTER_MAX_KEYS (16) still holds it: 15 keys.
 * ------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------------
 * sys_device: remote devices (sys_device/SYS_DEVICE_REMOTE.MD, sys_device_remote.h). CREATE_REMOTE (DEVICE contract,
 * the next free 0x00xx), the record, the call without the registry lock through the hook sys_remote sets,
 * `sys_device_find_remote(board, remote_id)`, the refusal of a pin_ref or a required device that is a remote one not
 * `blocking`, ERR_DEV_REMOTE_DOWN and ERR_DEV_REMOTE_NOT_WIRED. The rest is sys_remote's (sys_remote/SYS_REMOTE.MD).
 * ------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------------
 * sys_event:
 *  - SYS_EVENT_LOCAL, a flag of the events tables next to SYS_EVENT_NOTIFY: an event that never goes to
 *    SYS_DATA_CONNECTOR_EVENTS_ALL, so never to another board (a fast source; what only an inline listener wants).
 *  - sys_event_publish of an event with another source than its own device: sys_remote publishes a remote device's
 *    events with its local id (SYS_REMOTE.MD "Events and errors").
 * ------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------------
 * errors, runit: the board as a source of errors. An error about another board as a whole, no device of it
 * (ERR_CONN_MIRROR_LOST: a mirror of its stream lost frames), has no device policy to answer it:
 *
 *   err_record_src_e  ERR_RECORD_SRC_BOARD = 2   [src BOARD][unix_ms u64][board u8], then the chain
 *
 * runit's error policy gets a policy per board next to the one per device (log only, stop the VM, the safe state),
 * set by the board config; the default logs it. A remote device's errors come as that device's (sys_remote), so its
 * device policy answers them. runit calls sys_remote_init in the boot when the board config has other boards.
 * ------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------------
 * vm_blocks (the device engine, vm_block_device.c), for remote devices (SYS_REMOTE.MD "A call"):
 *  - ERR_SESSION_BUSY is no failure: a CHANGE keeps its newest values and sends them at a later pass; an EDGE waits
 *    and goes at a later pass; a second edge while one waits is reported (ERR_SESSION_BUSY).
 *  - A CHANGE whose write failed there (its answer ERROR, its timeout) counts as not sent: it goes again at the next
 *    pass.
 *  - A READ of an async remote device answering ERR_WATCH_NO_VALUE (its first copy not there yet) leaves the outputs
 *    as they are and is not reported.
 * ------------------------------------------------------------------------- */
