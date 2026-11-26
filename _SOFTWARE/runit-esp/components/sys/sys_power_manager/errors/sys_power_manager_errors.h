#pragma once

#include "errors.h"
#include "sys_power_manager_id.h"

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_PWRM)
 * ------------------------------------------------------------------------- */

#define ERR_SYS_POWER_MANAGER_MAP(X, M)                                                                                                                                                  \
  X(M, ERR_PWRM_NOT_READY,        1, ERR_SEV_HIGH,   {}) /* stopped: before the first LAYOUT */                                                                                         \
  X(M, ERR_PWRM_BUDGET_EXCEEDED,  2, ERR_SEV_MEDIUM, { uint8_t id; uint32_t requested_mW; uint32_t available_mW; }) /* a rail change that doesn't fit */                                \
  X(M, ERR_PWRM_BELOW_ALLOCATION, 3, ERR_SEV_MEDIUM, { uint32_t allocated_mW; uint32_t limit_mW; }) /* the budget shrank under what's committed */                                       \
  X(M, ERR_PWRM_FAULT,            4, ERR_SEV_MEDIUM, { uint16_t source; uint8_t channel; uint8_t kind; uint8_t response; int32_t value; }) /* a hardware fault and the response taken */ \
  X(M, ERR_PWRM_RAIL_TAKEN,       5, ERR_SEV_MEDIUM, { uint8_t id; }) /* rail already registered */                                                                                    \
  X(M, ERR_PWRM_NOT_RAIL,         6, ERR_SEV_HIGH,   { uint8_t id; }) /* a matrix row whose target is neither the board nor a rail of it */

ERR_TAGS_DECLARE(MODULE_ID_PWRM, ERR_SYS_POWER_MANAGER_MAP)
