#pragma once

#include "errors.h"
#include "sys_remote_id.h"

#define ERR_SYS_REMOTE_MAP(X, M)                                                                                                                                                              \
  X(M, ERR_REMOTE_NOT_WIRED, 1, ERR_SEV_MEDIUM, { uint8_t board; }) /* a wired remote device's board is reached by a way that is not wired: its calls do not wait (once until wired again) */ \
  X(M, ERR_REMOTE_SUBS_FULL, 2, ERR_SEV_MEDIUM, { uint8_t id; uint8_t max; }) /* SUBSCRIBE with every CONFIG_SYS_REMOTE_SUBSCRIPTIONS slot taken */

ERR_TAGS_DECLARE(MODULE_ID_REMOTE, ERR_SYS_REMOTE_MAP)
