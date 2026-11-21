#pragma once

#include "errors.h"
#include "sys_data_provider_id.h"

#define ERR_SYS_DATA_PROVIDER_MAP(X, M)                                                                                          \
  X(M, ERR_PROV_NOT_FOUND,    1, ERR_SEV_MEDIUM, { uint8_t provider; })                                                          \
  X(M, ERR_PROV_ID_TAKEN,     2, ERR_SEV_MEDIUM, { uint8_t provider; }) /* create: id taken */                                   \
  X(M, ERR_PROV_IN_USE,       3, ERR_SEV_LOW,    { uint8_t provider; }) /* create: a provider only one of may exist */           \
  X(M, ERR_PROV_SYSTEM,       4, ERR_SEV_MEDIUM, { uint8_t provider; }) /* remove: the board's own */                            \
  X(M, ERR_PROV_TYPE_UNKNOWN, 5, ERR_SEV_HIGH,   { uint8_t type; }) /* CREATE with unknown type */                               \
  X(M, ERR_PROV_UNKNOWN_OP,   6, ERR_SEV_HIGH,   { uint16_t op; }) /* op not in contract / type table, or missing side */        \
  X(M, ERR_PROV_ID,           7, ERR_SEV_NONE,   { uint8_t provider; }) /* context: the provider of a call or create */

ERR_TAGS_DECLARE(MODULE_ID_PROVIDER, ERR_SYS_DATA_PROVIDER_MAP)
