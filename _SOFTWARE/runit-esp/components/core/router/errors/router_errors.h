#pragma once

#include "errors.h"

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_ROUTER)
 * ------------------------------------------------------------------------- */

// Codes 4, 5 and 8 are unused: never assign them. A tag about a command carries its src (who sent it).
#define ERR_ROUTER_MAP(X)                                                                                    \
  X(ERR_ROUTER_KEY,         ERR_TAG(ERR_MOD_ROUTER, 1),  ERR_SEV_NONE,   { uint8_t key; }) /* context only: route step */ \
  X(ERR_ROUTER_NO_KEY,      ERR_TAG(ERR_MOD_ROUTER, 2),  ERR_SEV_HIGH,   {}) /* input ended where a key was expected */ \
  X(ERR_ROUTER_UNKNOWN_KEY, ERR_TAG(ERR_MOD_ROUTER, 3),  ERR_SEV_HIGH,   { uint8_t key; })                  \
  X(ERR_ROUTER_KEY_TAKEN,   ERR_TAG(ERR_MOD_ROUTER, 6),  ERR_SEV_HIGH,   { uint8_t key; })                  \
  X(ERR_ROUTER_TABLE_FULL,  ERR_TAG(ERR_MOD_ROUTER, 7),  ERR_SEV_HIGH,   { uint8_t key; })                  \
  X(ERR_ROUTER_QUEUE_FULL,  ERR_TAG(ERR_MOD_ROUTER, 9),  ERR_SEV_MEDIUM, { uint8_t src; uint8_t urgent; }) /* command not queued */ \
  X(ERR_ROUTER_FRAME_LEN,   ERR_TAG(ERR_MOD_ROUTER, 10), ERR_SEV_MEDIUM, { uint8_t src; uint16_t len; uint16_t max; }) /* request too short or too long */ \
  X(ERR_ROUTER_BUSY,        ERR_TAG(ERR_MOD_ROUTER, 11), ERR_SEV_MEDIUM, {}) /* router_run_urgent: another fn waits to run */

ERR_TAGS_DECLARE(ERR_ROUTER_MAP)
