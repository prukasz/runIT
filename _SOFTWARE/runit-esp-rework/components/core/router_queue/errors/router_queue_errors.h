#pragma once

#include "errors.h"

MODULE_ID_DECLARE(ROUTER_QUEUE, 0x24)
#define ERR_MOD_ROUTER_QUEUE MODULE_ID_ROUTER_QUEUE

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_ROUTER_QUEUE): a command that could not be queued or an urgent fn that could not run.
 * ------------------------------------------------------------------------- */

#define ERR_ROUTER_QUEUE_MAP(X)                                                                                    \
  X(ERR_ROUTER_QUEUE_FULL, ERR_TAG(ERR_MOD_ROUTER_QUEUE, 1), ERR_SEV_MEDIUM, { uint8_t src; uint8_t urgent; }) /* command not queued */ \
  X(ERR_ROUTER_FRAME_LEN,  ERR_TAG(ERR_MOD_ROUTER_QUEUE, 2), ERR_SEV_MEDIUM, { uint8_t src; uint16_t len; uint16_t max; }) /* request too short or too long */ \
  X(ERR_ROUTER_BUSY,       ERR_TAG(ERR_MOD_ROUTER_QUEUE, 3), ERR_SEV_MEDIUM, {}) /* router_run_urgent: another fn waits to run */

ERR_TAGS_DECLARE(ERR_ROUTER_QUEUE_MAP)
