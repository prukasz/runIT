#pragma once

#include "errors.h"
#include "sys_watch_id.h"

/* Every tag but FULL and NO_VALUE/STALE carries the watch's id. */
#define ERR_SYS_WATCH_MAP(X, M)                                                                                         \
  X(M, ERR_WATCH_NOT_FOUND, 1, ERR_SEV_MEDIUM, { uint8_t id; })                                                         \
  X(M, ERR_WATCH_NOT_READ,  2, ERR_SEV_MEDIUM, { uint8_t id; }) /* ADD/read: request is no read */                      \
  X(M, ERR_WATCH_NO_VALUE,  3, ERR_SEV_LOW,    { uint8_t board; }) /* cached read: no valid copy yet */                 \
  X(M, ERR_WATCH_STALE,     4, ERR_SEV_MEDIUM, { uint8_t board; uint16_t age_ms; }) /* cached read: copy too old */

ERR_TAGS_DECLARE(MODULE_ID_WATCH, ERR_SYS_WATCH_MAP)
