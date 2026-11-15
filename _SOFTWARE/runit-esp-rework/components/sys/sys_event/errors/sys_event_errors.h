#pragma once

#include "errors.h"

MODULE_ID_DECLARE(SYS_EVENT, 0x25)  //@alias System events @description Event bus and forwarding module ID
#define ERR_MOD_SYS_EVENT MODULE_ID_SYS_EVENT

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_EVENT).
 * ------------------------------------------------------------------------- */

#define ERR_SYS_EVENT_MAP(X)                                                                                     \
  X(ERR_EVENT_QUEUE_FULL, ERR_TAG(ERR_MOD_SYS_EVENT, 2), ERR_SEV_LOW,    { uint16_t source; uint32_t event; uint32_t dropped; }) \
  X(ERR_EVENT_TABLE_FULL, ERR_TAG(ERR_MOD_SYS_EVENT, 3), ERR_SEV_MEDIUM, {})                                    \
  X(ERR_EVENT_NOT_FOUND,  ERR_TAG(ERR_MOD_SYS_EVENT, 4), ERR_SEV_MEDIUM, { uint16_t id; }) /* subscription handle */ \
  X(ERR_EVENT_BAD_ARG,    ERR_TAG(ERR_MOD_SYS_EVENT, 5), ERR_SEV_HIGH,   {})                                    \
  X(ERR_EVENT_DELIVERY,   ERR_TAG(ERR_MOD_SYS_EVENT, 8), ERR_SEV_NONE,   { uint16_t source; uint32_t event; uint16_t channel; }) /* context: a listener failed */

ERR_TAGS_DECLARE(ERR_SYS_EVENT_MAP)
