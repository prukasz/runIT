#pragma once

#include "errors.h"
#include "sys_event_id.h"

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_SYS_EVENT).
 * ------------------------------------------------------------------------- */

#define ERR_SYS_EVENT_MAP(X, M)                                                                                         \
  X(M, ERR_EVENT_QUEUE_FULL, 1, ERR_SEV_LOW,    { uint16_t source; uint32_t event; uint32_t dropped; })                 \
  X(M, ERR_EVENT_NOT_FOUND,  2, ERR_SEV_MEDIUM, { uint16_t id; }) /* subscription handle */                             \
  X(M, ERR_EVENT_DELIVERY,   3, ERR_SEV_NONE,   { uint16_t source; uint32_t event; uint16_t channel; }) /* context: a listener failed */

ERR_TAGS_DECLARE(MODULE_ID_SYS_EVENT, ERR_SYS_EVENT_MAP)
