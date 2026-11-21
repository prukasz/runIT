#pragma once

/**
 * @file core_errors.h
 * @brief The core tags (module 0): the errors of the errors component itself and the generic ones every module
 * raises (ERR_ESP_ERR, ERR_NULL_PTR, ERR_RANGE / ERR_RANGE_F). Included by errors.h; the messages are in
 * errors_core.json, next to this map. Registered in errors.c. Not on its own: errors.h includes it, after the
 * types and macros it needs (err_sev_e, ERR_TAGS_DECLARE).
 */

#include <stdint.h>
#include "esp_err.h"

/** @brief The module of the core errors: 0, no module (module_id.h). */
enum { MODULE_ID_CORE = 0 };

#define ERR_CORE_MAP(X, M)                                                                                              \
  X(M, ERR_POOL_EMPTY, 1,  ERR_SEV_LOW,      {}) /* no block for a new error */                                         \
  X(M, ERR_ESP_ERR,    2,  ERR_SEV_HIGH,     { esp_err_t code; }) /* ESP-IDF call failed */                             \
  X(M, ERR_RECORD_CUT, 3,  ERR_SEV_MEDIUM,   { uint16_t len; }) /* err_record_parse: cut short */                       \
  X(M, ERR_NULL_PTR,   4,  ERR_SEV_HIGH,     {}) /* a C function got a NULL pointer: its caller's bug */                \
  X(M, ERR_RANGE,      5,  ERR_SEV_HIGH,     { int32_t value; int32_t min; int32_t max; }) /* ERR_CHECK_RANGE */        \
  X(M, ERR_RANGE_F,    6,  ERR_SEV_HIGH,     { float value; float min; float max; }) /* ERR_CHECK_RANGE, float v */     \
  X(M, ERR_BAD_ARG,    7,  ERR_SEV_HIGH,     {}) /* invalid argument or request value */                                \
  X(M, ERR_NO_MEM,     8,  ERR_SEV_CRITICAL, { uint32_t size; }) /* out of memory (bytes requested) */                  \
  X(M, ERR_FULL,       9,  ERR_SEV_MEDIUM,   { uint16_t max; }) /* fixed table or slot pool reached its limit */        \
  X(M, ERR_BUSY,       10, ERR_SEV_MEDIUM,   {}) /* another operation is already in progress */                         \
  X(M, ERR_BAD_OFFSET, 11, ERR_SEV_MEDIUM,   { uint32_t got; uint32_t want; }) /* chunked transfer offset mismatch */

ERR_TAGS_DECLARE(MODULE_ID_CORE, ERR_CORE_MAP)
