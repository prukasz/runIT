#pragma once

#include "errors.h"

// ERR_MOD_SYS_WATCH = 19 (err_modules.h). Every tag but FULL carries the watch's id.
#define ERR_SYS_WATCH_MAP(X)                                                                                           \
  X(ERR_WATCH_NOT_FOUND, ERR_TAG(ERR_MOD_SYS_WATCH, 1), ERR_SEV_MEDIUM, { uint8_t id; })                               \
  X(ERR_WATCH_NOT_READ,  ERR_TAG(ERR_MOD_SYS_WATCH, 2), ERR_SEV_MEDIUM, { uint8_t id; }) /* add: the request is no read */ \
  X(ERR_WATCH_BAD_ARG,   ERR_TAG(ERR_MOD_SYS_WATCH, 3), ERR_SEV_MEDIUM, { uint8_t id; }) /* add: id 0, period 0, a bad trigger or target */ \
  X(ERR_WATCH_FULL,      ERR_TAG(ERR_MOD_SYS_WATCH, 4), ERR_SEV_MEDIUM, {}) /* every CONFIG_SYS_WATCH_MAX slot taken */ \
  X(ERR_WATCH_NO_VALUE,  ERR_TAG(ERR_MOD_SYS_WATCH, 5), ERR_SEV_LOW,    { uint8_t board; }) /* cached read, no wait: no copy yet */ \
  X(ERR_WATCH_STALE,     ERR_TAG(ERR_MOD_SYS_WATCH, 6), ERR_SEV_MEDIUM, { uint8_t board; uint16_t age_ms; }) /* cached read, no wait: the copy too old */

ERR_TAGS_DECLARE(ERR_SYS_WATCH_MAP)
