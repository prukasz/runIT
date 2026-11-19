#pragma once

#include "errors.h"

#define ERR_SYS_OTA_MAP(X)                                                                                                     \
  X(ERR_OTA_BUSY,       ERR_TAG(ERR_MOD_SYS_OTA, 1), ERR_SEV_MEDIUM, {})                               /* the VM runs: stop it first */ \
  X(ERR_OTA_NOT_OPEN,   ERR_TAG(ERR_MOD_SYS_OTA, 2), ERR_SEV_MEDIUM, {})                               /* no BEGIN before it */ \
  X(ERR_OTA_TOO_BIG,    ERR_TAG(ERR_MOD_SYS_OTA, 3), ERR_SEV_MEDIUM, { uint32_t len; uint32_t max; })  /* past the slot or the size BEGIN gave */ \
  X(ERR_OTA_OFFSET,     ERR_TAG(ERR_MOD_SYS_OTA, 4), ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; }) /* a write out of order */ \
  X(ERR_OTA_INCOMPLETE, ERR_TAG(ERR_MOD_SYS_OTA, 5), ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; }) /* COMMIT before every byte */ \
  X(ERR_OTA_BAD_IMAGE,  ERR_TAG(ERR_MOD_SYS_OTA, 6), ERR_SEV_HIGH,   {})                               /* the image fails its own check */

ERR_TAGS_DECLARE(ERR_SYS_OTA_MAP)
