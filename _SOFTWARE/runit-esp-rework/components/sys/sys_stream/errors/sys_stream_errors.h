#pragma once

#include "errors.h"

/* The module's one number: root key, errors (ERR_MOD_SYS_STREAM = MODULE_ID_STREAM). */
MODULE_ID_DECLARE(STREAM, 0x29)  //@alias Data streams @description Periodic telemetry stream service module ID
#define ERR_MOD_SYS_STREAM MODULE_ID_STREAM

// Every tag but BAD_ARG carries the stream's id. A source provider's failure (open, send) is its ERR_PROV_*, which
// names it; a subscriber's or remote sink's is the session's (ERR_SESSION_NO_ROUTE).
#define ERR_SYS_STREAM_MAP(X)                                                                                          \
  /* codes 1, 5, 10, 12, 13 and 14 are unused: never assign them */                                                    \
  X(ERR_STREAM_NOT_FOUND,    ERR_TAG(ERR_MOD_SYS_STREAM, 2),  ERR_SEV_MEDIUM, { uint8_t stream; })                     \
  X(ERR_STREAM_EXISTS,       ERR_TAG(ERR_MOD_SYS_STREAM, 3),  ERR_SEV_LOW,    { uint8_t stream; })                     \
  X(ERR_STREAM_SYSTEM,       ERR_TAG(ERR_MOD_SYS_STREAM, 4),  ERR_SEV_LOW,    { uint8_t stream; }) /* remove: a system stream */ \
  X(ERR_STREAM_TARGETS_FULL, ERR_TAG(ERR_MOD_SYS_STREAM, 6),  ERR_SEV_MEDIUM, { uint8_t stream; }) /* source, sink, subscribe, listen: every target slot taken */ \
  X(ERR_STREAM_TOO_LONG,     ERR_TAG(ERR_MOD_SYS_STREAM, 7),  ERR_SEV_MEDIUM, { uint8_t stream; uint16_t len; uint16_t max; }) /* send: past the stream's own limit */ \
  X(ERR_STREAM_QUEUE_FULL,   ERR_TAG(ERR_MOD_SYS_STREAM, 8),  ERR_SEV_LOW,    { uint8_t stream; uint16_t len; }) /* payload dropped */ \
  X(ERR_STREAM_BAD_ARG,      ERR_TAG(ERR_MOD_SYS_STREAM, 9),  ERR_SEV_HIGH,   {})                                      \
  X(ERR_STREAM_FULL,         ERR_TAG(ERR_MOD_SYS_STREAM, 11), ERR_SEV_MEDIUM, { uint8_t stream; }) /* every CONFIG_SYS_STREAM_MAX slot taken */ \
  X(ERR_STREAM_DIRECTION,    ERR_TAG(ERR_MOD_SYS_STREAM, 15), ERR_SEV_MEDIUM, { uint8_t stream; }) /* send on a stream with a source, or source on a system stream */

ERR_TAGS_DECLARE(ERR_SYS_STREAM_MAP)
