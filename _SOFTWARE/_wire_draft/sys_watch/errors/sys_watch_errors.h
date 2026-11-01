#pragma once

// What step 4 adds to components/sys/sys_watch/errors/sys_watch_errors.h (ERR_SYS_WATCH_MAP), after ERR_WATCH_FULL:
#define ERR_SYS_WATCH_CACHED_MAP(X)                                                                                      X(ERR_WATCH_NO_VALUE,  ERR_TAG(ERR_MOD_SYS_WATCH, 5), ERR_SEV_LOW,    { uint8_t board; }) /* cached read, no wait: no copy yet */   X(ERR_WATCH_STALE,     ERR_TAG(ERR_MOD_SYS_WATCH, 6), ERR_SEV_MEDIUM, { uint8_t board; uint16_t age_ms; }) /* cached read, no wait: the copy too old */
