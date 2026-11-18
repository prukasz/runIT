#pragma once

#include "errors.h"
#include "sys_data_provider_id.h"

// Tags 11..15 belong to the typed registry / router ops.
#define ERR_SYS_DATA_PROVIDER_MAP(X, M)                                                                                          \
  X(M, ERR_PROV_NOT_FOUND,    1,  ERR_SEV_MEDIUM,   { uint8_t provider; })                                                       \
  X(M, ERR_PROV_EXISTS,       2,  ERR_SEV_MEDIUM,   { uint8_t provider; }) /* create: id taken */                                \
  X(M, ERR_PROV_FULL,         3,  ERR_SEV_MEDIUM,   {}) /* every CONFIG_SYS_DATA_PROVIDER_MAX slot taken */                      \
  X(M, ERR_PROV_IN_USE,       4,  ERR_SEV_LOW,      { uint8_t provider; }) /* remove: a send in flight */                        \
  X(M, ERR_PROV_SYSTEM,       5,  ERR_SEV_LOW,      { uint8_t provider; }) /* remove: the board's own */                         \
  X(M, ERR_PROV_DOWN,         6,  ERR_SEV_MEDIUM,   { uint8_t provider; }) /* send: link down */                                 \
  X(M, ERR_PROV_TOO_LONG,     7,  ERR_SEV_MEDIUM,   { uint8_t provider; uint16_t len; uint16_t max; }) /* send: never cut */     \
  X(M, ERR_PROV_SEND,         8,  ERR_SEV_NONE,     { uint8_t provider; }) /* context: driver's send failed */                   \
  X(M, ERR_PROV_BUSY,         9,  ERR_SEV_LOW,      { uint8_t provider; }) /* send queue full */                                 \
  X(M, ERR_PROV_BAD_ID,       11, ERR_SEV_HIGH,     { uint8_t provider; }) /* ID_NONE for instance op, or vice versa for LIST */ \
  X(M, ERR_PROV_TYPE_UNKNOWN, 12, ERR_SEV_HIGH,     { uint8_t type; }) /* CREATE with unknown type */                            \
  X(M, ERR_PROV_UNKNOWN_OP,   13, ERR_SEV_HIGH,     { uint16_t op; }) /* op not in contract / type table, or missing side */     \
  X(M, ERR_PROV_NO_MEM,       15, ERR_SEV_CRITICAL, { uint8_t provider; uint16_t size; }) /* calloc failure in CREATE */

ERR_TAGS_DECLARE(MODULE_ID_PROVIDER, ERR_SYS_DATA_PROVIDER_MAP)
