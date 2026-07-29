#pragma once
#include <stdint.h>
#include "esp_err.h"

// Owners for the sys_errors component itself (config + telemetry plumbing).
#define SYS_ERRORS_OWNER_MAP(X)                                 \
  X(OWNER_SYS_ERRORS_BASE, 0xA800, "OWNER_SYS_ERRORS_BASE")     \
  X(OWNER_SYS_ERRORS_CONFIG, 0xA801, "OWNER_SYS_ERRORS_CONFIG")

#define SYS_ERROR_BASE_MAP(X)                                                       \
  X(ERR_NO_HANDLE, struct { uint8_t unused; })                                      \
  X(ERR_NULL_PTR, struct { uint8_t unused; })                                       \
  X(ERR_INVALID_VAL_UI32, struct { uint32_t val; uint32_t min; uint32_t max; })    \
  X(ERR_INVALID_VAL_I32, struct { int32_t val; int32_t min; int32_t max; })        \
  X(ERR_INVALID_VAL_F, struct { float val; float min; float max; })                \
  X(ERR_DEV_DEP_FAILED, struct { uint8_t dev_id; })                                 \
  X(ERR_DEP_FAILED, struct { uint8_t unused; })                                     \
  X(ERR_ESP_ERR, struct { esp_err_t esp_code; })                                    \
  X(ERR_BASE_NO_MEM, struct { uint8_t unused; })                                    \
  X(ERR_BASE_NOT_SUPPORTED, struct { uint8_t unused; })                             \
  X(ERR_BASE_NOT_FOUND, struct { uint8_t unused; })                                 \
  X(ERR_BASE_INVALID_STATE, struct { uint8_t unused; })
