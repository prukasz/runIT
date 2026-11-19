#pragma once

#include "errors.h"
#include "sys_log_id.h"

#define ERR_SYS_LOG_MAP(X, M)                                                                                           \
  X(M, ERR_LOG_BAD_LEVEL, 1, ERR_SEV_HIGH, { uint8_t level; uint8_t max; }) /* above CONFIG_LOG_MAXIMUM_LEVEL */

ERR_TAGS_DECLARE(MODULE_ID_LOG, ERR_SYS_LOG_MAP)
