#pragma once

#include "errors.h"
#include "data_matrix_id.h"

// A provider's own failure (open, send) is its ERR_PROV_*, a session's its ERR_SESSION_*: they name the provider.
#define ERR_DATA_MATRIX_MAP(X, M)                                                                                                                                     \
  X(M, ERR_DATA_MATRIX_FULL,       1, ERR_SEV_MEDIUM, {}) /* every role, route or callback slot taken */                                                              \
  X(M, ERR_DATA_MATRIX_PAIR,       2, ERR_SEV_MEDIUM, { uint8_t from; uint8_t to; }) /* the kinds of the ends: not a pair, or a provider without the role it needs */ \
  X(M, ERR_DATA_MATRIX_ROLE,       3, ERR_SEV_MEDIUM, { uint8_t provider; }) /* the provider has another role */                                                      \
  X(M, ERR_DATA_MATRIX_TOO_LONG,   4, ERR_SEV_MEDIUM, { uint16_t len; uint16_t max; }) /* send: past the frame */                                                     \
  X(M, ERR_DATA_MATRIX_QUEUE_FULL, 5, ERR_SEV_LOW,    { uint8_t stream; uint16_t len; }) /* payload dropped */                                                        \
  X(M, ERR_DATA_MATRIX_BAD_ARG,    6, ERR_SEV_HIGH,   {})

ERR_TAGS_DECLARE(MODULE_ID_DATA_MATRIX, ERR_DATA_MATRIX_MAP)
