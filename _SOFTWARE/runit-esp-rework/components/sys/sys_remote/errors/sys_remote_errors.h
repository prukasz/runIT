#pragma once

#include "errors.h"

/* The module's one number: root key (REMOTE) and errors (ERR_MOD_SYS_REMOTE = MODULE_ID_REMOTE). */
MODULE_ID_DECLARE(REMOTE, 0x2F)  //@alias Remote devices @description Cross-board remote device bridge module ID
#define ERR_MOD_SYS_REMOTE MODULE_ID_REMOTE

#define ERR_SYS_REMOTE_MAP(X)                                                                                          \
  X(ERR_REMOTE_NOT_WIRED, ERR_TAG(ERR_MOD_SYS_REMOTE, 1), ERR_SEV_MEDIUM, { uint8_t board; }) /* a wired remote device's board is reached by a way that is not wired: its calls do not wait (once until wired again) */ \
  X(ERR_REMOTE_SUBS_FULL, ERR_TAG(ERR_MOD_SYS_REMOTE, 2), ERR_SEV_MEDIUM, { uint8_t id; uint8_t max; }) /* SUBSCRIBE with every CONFIG_SYS_REMOTE_SUBSCRIPTIONS slot taken */

ERR_TAGS_DECLARE(ERR_SYS_REMOTE_MAP)
