#pragma once

#include "errors.h"

#define ERR_SYS_CONNECTOR_MAP(X)                                                                                  \
  X(ERR_CONN_BAD_ID,        ERR_TAG(ERR_MOD_SYS_CONNECTOR, 1),  ERR_SEV_HIGH,   { uint8_t id; }) /* outside 0..CONFIG_SYS_CONNECTOR_MAX-1 */ \
  X(ERR_CONN_NOT_FOUND,     ERR_TAG(ERR_MOD_SYS_CONNECTOR, 2),  ERR_SEV_MEDIUM, { uint8_t id; })              \
  X(ERR_CONN_EXISTS,        ERR_TAG(ERR_MOD_SYS_CONNECTOR, 3),  ERR_SEV_LOW,    { uint8_t id; })              \
  X(ERR_CONN_PROTECTED,     ERR_TAG(ERR_MOD_SYS_CONNECTOR, 4),  ERR_SEV_LOW,    { uint8_t id; }) /* system connector: no remove, keeps one binding */ \
  X(ERR_CONN_NO_PROVIDER,   ERR_TAG(ERR_MOD_SYS_CONNECTOR, 5),  ERR_SEV_MEDIUM, { uint8_t provider; })        \
  X(ERR_CONN_BINDINGS_FULL, ERR_TAG(ERR_MOD_SYS_CONNECTOR, 6),  ERR_SEV_MEDIUM, { uint8_t id; })              \
  X(ERR_CONN_TOO_LONG,      ERR_TAG(ERR_MOD_SYS_CONNECTOR, 7),  ERR_SEV_MEDIUM, { uint8_t id; uint8_t provider; uint16_t len; uint16_t max; }) /* frame not sent there */ \
  X(ERR_CONN_QUEUE_FULL,    ERR_TAG(ERR_MOD_SYS_CONNECTOR, 8),  ERR_SEV_LOW,    { uint8_t id; uint16_t len; }) /* payload dropped */ \
  X(ERR_CONN_BAD_ARG,       ERR_TAG(ERR_MOD_SYS_CONNECTOR, 9),  ERR_SEV_HIGH,   {})                           \
  X(ERR_CONN_SEND,          ERR_TAG(ERR_MOD_SYS_CONNECTOR, 10), ERR_SEV_NONE,   { uint8_t id; uint8_t provider; uint32_t endpoint; }) /* context: provider send failed */

ERR_TAGS_DECLARE(ERR_SYS_CONNECTOR_MAP)
