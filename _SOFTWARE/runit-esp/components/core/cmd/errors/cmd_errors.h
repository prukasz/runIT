#pragma once

#include "errors.h"

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_CMD)
 * ------------------------------------------------------------------------- */

#define ERR_CMD_MAP(X)                                                                                    \
  X(ERR_CMD_BAD_LEN,   ERR_TAG(ERR_MOD_CMD, 1), ERR_SEV_HIGH, { uint16_t got; uint16_t need; }) /* payload size */ \
  X(ERR_CMD_OUT_FULL,  ERR_TAG(ERR_MOD_CMD, 2), ERR_SEV_HIGH, { uint16_t need; uint16_t cap; }) /* answer does not fit the link */ \
  X(ERR_CMD_BAD_ARG,   ERR_TAG(ERR_MOD_CMD, 3), ERR_SEV_HIGH, {}) /* invalid argument value */ \
  X(ERR_CMD_DIRECTION, ERR_TAG(ERR_MOD_CMD, 4), ERR_SEV_HIGH, { uint8_t dir; }) /* the command has no such side, or dir is neither read nor write */

ERR_TAGS_DECLARE(ERR_CMD_MAP)
