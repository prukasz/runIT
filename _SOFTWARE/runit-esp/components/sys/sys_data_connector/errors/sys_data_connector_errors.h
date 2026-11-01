#pragma once

#include "errors.h"

// Every tag but BAD_ARG carries the connector's id. A provider's own failure (open, send) is its ERR_PROV_*, which
// names it; a subscriber's is the session's (ERR_SESSION_NO_ROUTE).
#define ERR_SYS_DATA_CONNECTOR_MAP(X)                                                                                  \
  /* codes 1, 5, 10, 12 and 13 are unused: never assign them */                                                                \
  X(ERR_CONN_NOT_FOUND,     ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 2),  ERR_SEV_MEDIUM, { uint8_t connector; })           \
  X(ERR_CONN_EXISTS,        ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 3),  ERR_SEV_LOW,    { uint8_t connector; })           \
  X(ERR_CONN_SYSTEM,        ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 4),  ERR_SEV_LOW,    { uint8_t connector; }) /* remove: a system connector */ \
  X(ERR_CONN_TARGETS_FULL,  ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 6),  ERR_SEV_MEDIUM, { uint8_t connector; }) /* bind: every binding slot taken; subscribe: every subscriber slot */ \
  X(ERR_CONN_TOO_LONG,      ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 7),  ERR_SEV_MEDIUM, { uint8_t connector; uint16_t len; uint16_t max; }) /* send: past the connector's own limit */ \
  X(ERR_CONN_QUEUE_FULL,    ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 8),  ERR_SEV_LOW,    { uint8_t connector; uint16_t len; }) /* payload dropped */ \
  X(ERR_CONN_BAD_ARG,       ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 9),  ERR_SEV_HIGH,   {})                               \
  X(ERR_CONN_FULL,          ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 11), ERR_SEV_MEDIUM, { uint8_t connector; }) /* every CONFIG_SYS_DATA_CONNECTOR_MAX slot taken */ \
  X(ERR_CONN_MIRROR_LOST,   ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 14), ERR_SEV_MEDIUM, { uint8_t connector; uint8_t board; uint16_t count; }) /* a mirror's frames lost on the way; codes 12 and 13 are kept */

ERR_TAGS_DECLARE(ERR_SYS_DATA_CONNECTOR_MAP)
