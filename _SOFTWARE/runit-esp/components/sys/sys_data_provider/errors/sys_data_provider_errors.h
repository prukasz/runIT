#pragma once

#include "errors.h"

// ERR_MOD_SYS_DATA_PROVIDER = 17 (err_modules.h). Every tag but FULL carries the provider's id.
#define ERR_SYS_DATA_PROVIDER_MAP(X)                                                                                   \
  X(ERR_PROV_NOT_FOUND, ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 1),  ERR_SEV_MEDIUM, { uint8_t provider; })   \
  X(ERR_PROV_EXISTS,    ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 2),  ERR_SEV_MEDIUM, { uint8_t provider; }) /* add: the id is taken */ \
  X(ERR_PROV_FULL,      ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 3),  ERR_SEV_MEDIUM, {}) /* every CONFIG_SYS_DATA_PROVIDER_MAX slot taken */ \
  X(ERR_PROV_IN_USE,    ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 4),  ERR_SEV_LOW,    { uint8_t provider; }) /* open, remove: the rx hook has an owner; remove: a send in progress */ \
  X(ERR_PROV_SYSTEM,    ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 5),  ERR_SEV_LOW,    { uint8_t provider; }) /* remove: the board's own */ \
  X(ERR_PROV_DOWN,      ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 6),  ERR_SEV_MEDIUM, { uint8_t provider; }) /* send: it carries nothing now */ \
  X(ERR_PROV_TOO_LONG,  ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 7),  ERR_SEV_MEDIUM, { uint8_t provider; uint16_t len; uint16_t max; }) /* send: not sent, never cut */ \
  X(ERR_PROV_SEND,      ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 8),  ERR_SEV_NONE,   { uint8_t provider; }) /* context: the driver's send failed */ \
  X(ERR_PROV_BUSY,      ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 9),  ERR_SEV_LOW,    { uint8_t provider; }) /* send: the driver's queue full; raised by the driver */ \
  X(ERR_PROV_NO_RX,     ERR_TAG(ERR_MOD_SYS_DATA_PROVIDER, 10), ERR_SEV_MEDIUM, { uint8_t provider; }) /* open with an rx: this provider delivers to no hook */

ERR_TAGS_DECLARE(ERR_SYS_DATA_PROVIDER_MAP)
