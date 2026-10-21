#pragma once

#include "errors.h"

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_LOG)
 * ------------------------------------------------------------------------- */

#define ERR_SYS_LOG_MAP(X)                                                                                    \
  X(ERR_LOG_BAD_LEVEL, ERR_TAG(ERR_MOD_SYS_LOG, 1), ERR_SEV_HIGH, { uint8_t level; uint8_t max; }) /* above CONFIG_LOG_MAXIMUM_LEVEL */

ERR_TAGS_DECLARE(ERR_SYS_LOG_MAP)
