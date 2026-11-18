#pragma once

#include "errors.h"
#include "router_id.h"

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_ROUTER): the route (a root key, a command of a module).
 * ------------------------------------------------------------------------- */

// A tag about a command carries its src (who sent it). A route step is KEY at the root, OP in a module.
#define ERR_ROUTER_MAP(X, M)                                                                                            \
  X(M, ERR_ROUTER_KEY,         1, ERR_SEV_NONE, { uint8_t key; }) /* context only: route step at the root */            \
  X(M, ERR_ROUTER_OP,          2, ERR_SEV_NONE, { uint8_t code; }) /* context only: route step in a module */           \
  X(M, ERR_ROUTER_NO_KEY,      3, ERR_SEV_HIGH, {}) /* the command ended where its key or its op code was expected */   \
  X(M, ERR_ROUTER_UNKNOWN_KEY, 4, ERR_SEV_HIGH, { uint8_t key; }) /* no module under the root key */                    \
  X(M, ERR_ROUTER_UNKNOWN_OP,  5, ERR_SEV_HIGH, { uint8_t code; }) /* no command of the module has the code */          \
  X(M, ERR_ROUTER_KEY_TAKEN,   6, ERR_SEV_HIGH, { uint8_t key; })                                                       \
  X(M, ERR_ROUTER_TABLE_FULL,  7, ERR_SEV_HIGH, { uint8_t key; })

ERR_TAGS_DECLARE(MODULE_ID_ROUTER, ERR_ROUTER_MAP)
