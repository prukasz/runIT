#pragma once

#include "errors.h"
#include "cmd_id.h"

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_CMD): shared handler errors (payload, answer, args, direction).
 * ------------------------------------------------------------------------- */

#define ERR_CMD_MAP(X, M)                                                                                               \
  X(M, ERR_CMD_BAD_LEN,   1, ERR_SEV_HIGH, { uint16_t got; uint16_t need; }) /* payload size */                         \
  X(M, ERR_CMD_OUT_FULL,  2, ERR_SEV_HIGH, { uint16_t need; uint16_t cap; }) /* answer does not fit the link */         \
  X(M, ERR_CMD_DIRECTION, 3, ERR_SEV_HIGH, { uint8_t dir; }) /* the command has no such side, or dir is neither read nor write */

ERR_TAGS_DECLARE(MODULE_ID_CMD, ERR_CMD_MAP)
