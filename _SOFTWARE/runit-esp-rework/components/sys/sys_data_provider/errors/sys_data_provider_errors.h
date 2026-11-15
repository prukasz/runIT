#pragma once

#include "errors.h"

/* The module's one number: the root key of its commands (router/) and the module of its errors. */
MODULE_ID_DECLARE(PROVIDER, 0x0D)
#define ERR_MOD_SYS_DATA_PROVIDER MODULE_ID_PROVIDER

// Tags 1..10 keep their historical wire numbers; 11..15 belong to the typed registry / router ops.
#define ERR_SYS_DATA_PROVIDER_MAP(X)                                                                                    \
  X(ERR_PROV_NOT_FOUND,    ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 1),  ERR_SEV_MEDIUM,   { uint8_t provider; })             \
  X(ERR_PROV_EXISTS,       ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 2),  ERR_SEV_MEDIUM,   { uint8_t provider; }) /* create: id taken */ \
  X(ERR_PROV_FULL,         ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 3),  ERR_SEV_MEDIUM,   {}) /* every CONFIG_SYS_DATA_PROVIDER_MAX slot taken */ \
  X(ERR_PROV_IN_USE,       ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 4),  ERR_SEV_LOW,      { uint8_t provider; }) /* open / remove: rx hook taken or send in flight */ \
  X(ERR_PROV_SYSTEM,       ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 5),  ERR_SEV_LOW,      { uint8_t provider; }) /* remove: the board's own */ \
  X(ERR_PROV_DOWN,         ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 6),  ERR_SEV_MEDIUM,   { uint8_t provider; }) /* send: link down */ \
  X(ERR_PROV_TOO_LONG,     ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 7),  ERR_SEV_MEDIUM,   { uint8_t provider; uint16_t len; uint16_t max; }) /* send: never cut */ \
  X(ERR_PROV_SEND,         ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 8),  ERR_SEV_NONE,     { uint8_t provider; }) /* context: driver's send failed */ \
  X(ERR_PROV_BUSY,         ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 9),  ERR_SEV_LOW,      { uint8_t provider; }) /* send queue full */ \
  X(ERR_PROV_NO_RX,        ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 10), ERR_SEV_MEDIUM,   { uint8_t provider; }) /* open with rx: NO_RX provider */ \
  X(ERR_PROV_BAD_ID,       ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 11), ERR_SEV_HIGH,     { uint8_t provider; }) /* ID_NONE for instance op, or vice versa for LIST */ \
  X(ERR_PROV_TYPE_UNKNOWN, ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 12), ERR_SEV_HIGH,     { uint8_t type; }) /* CREATE with unknown type */ \
  X(ERR_PROV_UNKNOWN_OP,   ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 13), ERR_SEV_HIGH,     { uint16_t op; }) /* op not in contract / type table, or missing side */ \
  X(ERR_PROV_LOCAL_OP,     ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 14), ERR_SEV_HIGH,     { uint16_t op; }) /* LOCAL op called via router */ \
  X(ERR_PROV_NO_MEM,       ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 15), ERR_SEV_CRITICAL, { uint8_t provider; uint16_t size; }) /* calloc failure in CREATE */

ERR_TAGS_DECLARE(ERR_SYS_DATA_PROVIDER_MAP)
