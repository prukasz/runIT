#pragma once

#include "errors.h"
#include "nvs_store_id.h"

#define ERR_NVS_STORE_MAP(X, M)                                                                                         \
  X(M, ERR_NVS_NO_CHUNK,  1, ERR_SEV_LOW,    { uint16_t chunk; })                                                       \
  X(M, ERR_NVS_NOT_FOUND, 2, ERR_SEV_LOW,    {}) /* get, erase: no such key */

ERR_TAGS_DECLARE(MODULE_ID_NVS, ERR_NVS_STORE_MAP)
