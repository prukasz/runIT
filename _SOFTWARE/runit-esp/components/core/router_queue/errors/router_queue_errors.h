#pragma once

#include "errors.h"
#include "router_queue_id.h"

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_ROUTER_QUEUE): a command that could not be queued, an urgent fn that could not run, a call
 * of this board that ended without its answer. A call's port is a port's name (data_matrix.h).
 * ------------------------------------------------------------------------- */

#define ERR_ROUTER_QUEUE_MAP(X, M)                                                                                                      \
  X(M, ERR_ROUTER_QUEUE_FULL,   1, ERR_SEV_MEDIUM, { uint8_t src; uint8_t urgent; }) /* command not queued */                           \
  X(M, ERR_ROUTER_FRAME_LEN,    2, ERR_SEV_MEDIUM, { uint8_t src; uint16_t len; uint16_t max; }) /* request too short or too long */    \
  X(M, ERR_ROUTER_BUSY,         3, ERR_SEV_MEDIUM, {}) /* router_run_urgent: another fn waits to run */                                \
  X(M, ERR_ROUTER_CALLS_FULL,   4, ERR_SEV_MEDIUM, {}) /* every CONFIG_ROUTER_CALLS slot in use */                                     \
  X(M, ERR_ROUTER_NO_LINK,      5, ERR_SEV_MEDIUM, { uint16_t port; }) /* a call to a port not linked from the router's */            \
  X(M, ERR_ROUTER_CALL_TIMEOUT, 6, ERR_SEV_MEDIUM, { uint16_t port; uint16_t seq; }) /* a call without an answer */                    \
  X(M, ERR_ROUTER_CALL_DOWN,    7, ERR_SEV_MEDIUM, { uint16_t port; }) /* the link the call went out on ended */                       \
  X(M, ERR_REMOTE,              8, ERR_SEV_NONE,   { uint8_t board; uint16_t seq; }) /* context: the chain below is that board's */    \
  X(M, ERR_ROUTER_WOULD_BLOCK,  9, ERR_SEV_HIGH,   {}) /* call_wait to this board on a router task: it would wait for itself */

ERR_TAGS_DECLARE(MODULE_ID_ROUTER_QUEUE, ERR_ROUTER_QUEUE_MAP)
