#pragma once

#include "errors.h"

MODULE_ID_DECLARE(ROUTER, 0x23)
#define ERR_MOD_ROUTER MODULE_ID_ROUTER

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_ROUTER): the route (a root key, a command of a module).
 * ------------------------------------------------------------------------- */

// A tag about a command carries its src (who sent it). A route step is KEY at the root, OP in a module.
#define ERR_ROUTER_MAP(X)                                                                                      \
  X(ERR_ROUTER_KEY,         ERR_TAG(ERR_MOD_ROUTER, 1),  ERR_SEV_NONE,   { uint8_t key; }) /* context only: route step at the root */   \
  X(ERR_ROUTER_OP,          ERR_TAG(ERR_MOD_ROUTER, 2),  ERR_SEV_NONE,   { uint8_t code; }) /* context only: route step in a module */ \
  X(ERR_ROUTER_NO_KEY,      ERR_TAG(ERR_MOD_ROUTER, 3),  ERR_SEV_HIGH,   {}) /* the command ended where its key or its op code was expected */ \
  X(ERR_ROUTER_UNKNOWN_KEY, ERR_TAG(ERR_MOD_ROUTER, 4),  ERR_SEV_HIGH,   { uint8_t key; }) /* no module under the root key */ \
  X(ERR_ROUTER_UNKNOWN_OP,  ERR_TAG(ERR_MOD_ROUTER, 5),  ERR_SEV_HIGH,   { uint8_t code; }) /* no command of the module has the code */ \
  X(ERR_ROUTER_KEY_TAKEN,   ERR_TAG(ERR_MOD_ROUTER, 6),  ERR_SEV_HIGH,   { uint8_t key; }) \
  X(ERR_ROUTER_TABLE_FULL,  ERR_TAG(ERR_MOD_ROUTER, 7),  ERR_SEV_HIGH,   { uint8_t key; })

ERR_TAGS_DECLARE(ERR_ROUTER_MAP)
