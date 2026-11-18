#pragma once

#include "errors.h"
#include "router_queue_id.h"

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_ROUTER_QUEUE): a command that could not be queued or an urgent fn that could not run.
 * ------------------------------------------------------------------------- */

#define ERR_ROUTER_QUEUE_MAP(X, M)                                                                                                 \
  X(M, ERR_ROUTER_QUEUE_FULL, 1, ERR_SEV_MEDIUM, { uint8_t src; uint8_t urgent; }) /* command not queued */                        \
  X(M, ERR_ROUTER_FRAME_LEN,  2, ERR_SEV_MEDIUM, { uint8_t src; uint16_t len; uint16_t max; }) /* request too short or too long */ \
  X(M, ERR_ROUTER_BUSY,       3, ERR_SEV_MEDIUM, {}) /* router_run_urgent: another fn waits to run */

ERR_TAGS_DECLARE(MODULE_ID_ROUTER_QUEUE, ERR_ROUTER_QUEUE_MAP)
