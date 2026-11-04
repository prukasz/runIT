#pragma once

#include "errors.h"

// ERR_MOD_SYS_REMOTE = 21 (err_modules.h).
#define ERR_SYS_REMOTE_MAP(X)                                                                                          \
  X(ERR_REMOTE_NOT_WIRED,   ERR_TAG(ERR_MOD_SYS_REMOTE, 2), ERR_SEV_MEDIUM, { uint8_t board; }) /* a wired remote device's board is reached by a way that is not wired: its calls do not wait (once until wired again) */ \
  X(ERR_REMOTE_LOST,        ERR_TAG(ERR_MOD_SYS_REMOTE, 3), ERR_SEV_MEDIUM, { uint8_t board; uint16_t count; }) /* events or errors of a remote device lost on the way: the gap in the subscription's seq */ \
  X(ERR_REMOTE_SUBS_FULL,   ERR_TAG(ERR_MOD_SYS_REMOTE, 4), ERR_SEV_MEDIUM, { uint16_t id; uint8_t max; }) /* SUBSCRIBE with every CONFIG_SYS_REMOTE_SUBSCRIPTIONS slot taken */ \
  X(ERR_REMOTE_ALIASES_FULL, ERR_TAG(ERR_MOD_SYS_REMOTE, 5), ERR_SEV_MEDIUM, { uint8_t board; uint8_t max; }) /* every CONFIG_SYS_REMOTE_ALIASES slot taken: a remote device's events and errors do not come */

ERR_TAGS_DECLARE(ERR_SYS_REMOTE_MAP)
