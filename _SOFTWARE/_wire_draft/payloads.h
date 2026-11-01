#pragma once

#include <stdint.h>

/* DRAFT: what is left of the wire design outside _wire_draft: the event flag and the remote events, the board as an
 * error source. The error record (err_cmd_t, err_record_cmd_t, err_record_parse) is in errors.h (ERRORS.MD).
 * Not built. Each section names the module it goes to. */

/* ----------------------------------------------------------------------------
 * sys_event:
 *  - SYS_EVENT_LOCAL, a flag of the events tables next to SYS_EVENT_NOTIFY: an event that never goes to
 *    SYS_DATA_CONNECTOR_EVENTS_ALL, so never to another board (a fast source; what only an inline listener wants).
 *  - sys_event_publish of an event with another source than its own device: sys_remote publishes a remote device's
 *    NOTIFY events with its local id (SYS_REMOTE.MD "Events"); here the others (EVENTS_ALL) too.
 * ------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------------
 * errors, runit: the board as a source of errors. An error about another board as a whole, no device of it
 * (ERR_CONN_MIRROR_LOST: a mirror of its stream lost frames), has no device policy to answer it:
 *
 *   err_record_src_e  ERR_RECORD_SRC_BOARD = 2   [src BOARD][unix_ms u64][board u8], then the chain
 *
 * runit's error policy gets a policy per board next to the one per device (log only, stop the VM, the safe state),
 * set by the board config; the default logs it. A remote device's errors come as that device's (sys_remote), so its
 * device policy answers them. runit calls sys_remote_init in the boot when the board has a link.
 * ------------------------------------------------------------------------- */
