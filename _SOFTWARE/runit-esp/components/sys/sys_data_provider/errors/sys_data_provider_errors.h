#pragma once

#include "errors.h"

// ERR_MOD_SYS_DATA_PROVIDER = 17 (err_modules.h). Every tag but FULL carries the provider's id.
#define ERR_SYS_DATA_PROVIDER_MAP(X)                                                                                   \
  X(ERR_PROV_NOT_FOUND, ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 1),  ERR_SEV_MEDIUM, { uint8_t link; uint32_t channel; })   \
  X(ERR_PROV_EXISTS,    ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 2),  ERR_SEV_MEDIUM, { uint8_t link; uint32_t channel; }) /* add: the id is taken */ \
  X(ERR_PROV_FULL,      ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 3),  ERR_SEV_MEDIUM, {}) /* every CONFIG_SYS_DATA_PROVIDER_MAX slot taken */ \
  X(ERR_PROV_IN_USE,    ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 4),  ERR_SEV_LOW,    { uint8_t link; uint32_t channel; }) /* open, remove, remote SEND: another user has it; remove: a send in progress */ \
  X(ERR_PROV_SYSTEM,    ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 5),  ERR_SEV_LOW,    { uint8_t link; uint32_t channel; }) /* remove: the board's own */ \
  X(ERR_PROV_DOWN,      ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 6),  ERR_SEV_MEDIUM, { uint8_t link; uint32_t channel; }) /* send: it carries nothing now */ \
  X(ERR_PROV_TOO_LONG,  ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 7),  ERR_SEV_MEDIUM, { uint8_t link; uint32_t channel; uint16_t len; uint16_t max; }) /* send: not sent, never cut */ \
  X(ERR_PROV_SEND,      ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 8),  ERR_SEV_NONE,   { uint8_t link; uint32_t channel; }) /* context: the driver's send failed */ \
  X(ERR_PROV_BUSY,      ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 9),  ERR_SEV_LOW,    { uint8_t link; uint32_t channel; }) /* send: the driver's queue full; raised by the driver */

ERR_TAGS_DECLARE(ERR_SYS_DATA_PROVIDER_MAP)
