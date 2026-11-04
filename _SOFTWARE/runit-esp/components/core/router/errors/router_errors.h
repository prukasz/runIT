#pragma once

#include "errors.h"

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_ROUTER)
 * ------------------------------------------------------------------------- */

// A tag about a command carries its src (who sent it). A route step is KEY at the root, OP in a module.
#define ERR_ROUTER_MAP(X)                                                                                    \
  X(ERR_ROUTER_KEY,         ERR_TAG(ERR_MOD_ROUTER, 1),  ERR_SEV_NONE,   { uint8_t key; }) /* context only: route step at the root */ \
  X(ERR_ROUTER_OP,          ERR_TAG(ERR_MOD_ROUTER, 2),  ERR_SEV_NONE,   { uint8_t code; }) /* context only: route step in a module */ \
  X(ERR_ROUTER_NO_KEY,      ERR_TAG(ERR_MOD_ROUTER, 3),  ERR_SEV_HIGH,   {}) /* the command ended where its key or its op code was expected */ \
  X(ERR_ROUTER_UNKNOWN_KEY, ERR_TAG(ERR_MOD_ROUTER, 4),  ERR_SEV_HIGH,   { uint8_t key; }) /* no module under the root key */ \
  X(ERR_ROUTER_UNKNOWN_OP,  ERR_TAG(ERR_MOD_ROUTER, 5),  ERR_SEV_HIGH,   { uint8_t code; }) /* no command of the module has the code */ \
  X(ERR_ROUTER_KEY_TAKEN,   ERR_TAG(ERR_MOD_ROUTER, 6),  ERR_SEV_HIGH,   { uint8_t key; })                  \
  X(ERR_ROUTER_TABLE_FULL,  ERR_TAG(ERR_MOD_ROUTER, 7),  ERR_SEV_HIGH,   { uint8_t key; })                  \
  X(ERR_ROUTER_QUEUE_FULL,  ERR_TAG(ERR_MOD_ROUTER, 8),  ERR_SEV_MEDIUM, { uint8_t src; uint8_t urgent; }) /* command not queued */ \
  X(ERR_ROUTER_FRAME_LEN,   ERR_TAG(ERR_MOD_ROUTER, 9),  ERR_SEV_MEDIUM, { uint8_t src; uint16_t len; uint16_t max; }) /* request too short or too long */ \
  X(ERR_ROUTER_BUSY,        ERR_TAG(ERR_MOD_ROUTER, 10), ERR_SEV_MEDIUM, {}) /* router_run_urgent: another fn waits to run */

ERR_TAGS_DECLARE(ERR_ROUTER_MAP)
