#pragma once

#include "errors.h"

#define ERR_SYS_DATA_CONNECTOR_MAP(X)                                                                                  \
  /* code 1 is unused: never assign it */                                                                              \
  X(ERR_CONN_NOT_FOUND,     ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 2),  ERR_SEV_MEDIUM, { uint8_t connector; })           \
  X(ERR_CONN_EXISTS,        ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 3),  ERR_SEV_LOW,    { uint8_t connector; })           \
  X(ERR_CONN_PROTECTED,     ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 4),  ERR_SEV_LOW,    { uint8_t connector; }) /* system connector: no remove, keeps one binding */ \
  X(ERR_CONN_NO_PROVIDER,   ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 5),  ERR_SEV_MEDIUM, { uint8_t provider; })            \
  X(ERR_CONN_BINDINGS_FULL, ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 6),  ERR_SEV_MEDIUM, { uint8_t connector; })           \
  X(ERR_CONN_TOO_LONG,      ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 7),  ERR_SEV_MEDIUM, { uint8_t connector; uint8_t provider; uint16_t len; uint16_t max; }) /* frame not sent there */ \
  X(ERR_CONN_QUEUE_FULL,    ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 8),  ERR_SEV_LOW,    { uint8_t connector; uint16_t len; }) /* payload dropped */ \
  X(ERR_CONN_BAD_ARG,       ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 9),  ERR_SEV_HIGH,   {})                               \
  X(ERR_CONN_SEND,          ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 10), ERR_SEV_NONE,   { uint8_t connector; uint8_t provider; uint32_t endpoint; }) /* context: provider send failed */ \
  X(ERR_CONN_FULL,          ERR_TAG(ERR_MOD_SYS_DATA_CONNECTOR, 11), ERR_SEV_MEDIUM, { uint8_t connector; }) /* every CONFIG_SYS_DATA_CONNECTOR_MAX slot taken */

ERR_TAGS_DECLARE(ERR_SYS_DATA_CONNECTOR_MAP)
