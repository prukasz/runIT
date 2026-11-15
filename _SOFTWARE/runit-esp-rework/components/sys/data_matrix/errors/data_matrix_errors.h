#pragma once

#include "errors.h"

/* The module's one number: root key of its commands (router/) and the module of its errors. */
MODULE_ID_DECLARE(DATA_MATRIX, 0x30)  //@alias Data matrix @description Roles of the providers and routes of the data that are not commands
#define ERR_MOD_DATA_MATRIX MODULE_ID_DATA_MATRIX

// A provider's own failure (open, send) is its ERR_PROV_*, a session's its ERR_SESSION_*: they name the provider.
#define ERR_DATA_MATRIX_MAP(X)                                                                                         \
  X(ERR_DATA_MATRIX_FULL,       ERR_TAG(ERR_MOD_DATA_MATRIX, 1), ERR_SEV_MEDIUM, {}) /* every role, route or callback slot taken */ \
  X(ERR_DATA_MATRIX_PAIR,       ERR_TAG(ERR_MOD_DATA_MATRIX, 2), ERR_SEV_MEDIUM, { uint8_t from; uint8_t to; }) /* the kinds of the ends: not a pair, or a provider without the role it needs */ \
  X(ERR_DATA_MATRIX_ROLE,       ERR_TAG(ERR_MOD_DATA_MATRIX, 3), ERR_SEV_MEDIUM, { uint8_t provider; }) /* the provider has another role */ \
  X(ERR_DATA_MATRIX_TOO_LONG,   ERR_TAG(ERR_MOD_DATA_MATRIX, 4), ERR_SEV_MEDIUM, { uint16_t len; uint16_t max; }) /* send: past the frame */ \
  X(ERR_DATA_MATRIX_QUEUE_FULL, ERR_TAG(ERR_MOD_DATA_MATRIX, 5), ERR_SEV_LOW,    { uint8_t stream; uint16_t len; }) /* payload dropped */ \
  X(ERR_DATA_MATRIX_BAD_ARG,    ERR_TAG(ERR_MOD_DATA_MATRIX, 6), ERR_SEV_HIGH,   {})

ERR_TAGS_DECLARE(ERR_DATA_MATRIX_MAP)
