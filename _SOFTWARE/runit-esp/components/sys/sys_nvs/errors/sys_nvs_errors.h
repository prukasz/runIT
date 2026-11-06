#pragma once

#include "errors.h"

// ERR_MOD_SYS_NVS = 22 (err_modules.h).
#define ERR_SYS_NVS_MAP(X)                                                                                             \
  X(ERR_NVS_BAD_ARG,  ERR_TAG(ERR_MOD_SYS_NVS, 1), ERR_SEV_MEDIUM, {}) /* append: no data, or no chunk left */         \
  X(ERR_NVS_OFFSET,   ERR_TAG(ERR_MOD_SYS_NVS, 2), ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; })                   \
  X(ERR_NVS_NO_CHUNK, ERR_TAG(ERR_MOD_SYS_NVS, 3), ERR_SEV_LOW,    { uint16_t chunk; })

ERR_TAGS_DECLARE(ERR_SYS_NVS_MAP)
