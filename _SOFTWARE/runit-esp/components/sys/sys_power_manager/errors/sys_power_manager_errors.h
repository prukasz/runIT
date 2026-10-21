#pragma once

#include "errors.h"

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_POWER_MANAGER)
 * ------------------------------------------------------------------------- */

#define ERR_SYS_POWER_MANAGER_MAP(X)                                                                                                                                                                                            \
  X(ERR_PWRM_NOT_READY,              ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 1), ERR_SEV_HIGH,   {})                                     /* before sys_power_manager_init */                                                         \
  X(ERR_PWRM_BUDGET_EXCEEDED,        ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 2), ERR_SEV_MEDIUM, { uint16_t id; uint32_t requested_mW; uint32_t available_mW; }) /* a rail change that doesn't fit */                                \
  X(ERR_PWRM_BELOW_ALLOCATION,       ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 3), ERR_SEV_MEDIUM, { uint32_t allocated_mW; uint32_t limit_mW; })              /* the budget shrank under what's committed */                          \
  X(ERR_PWRM_FAULT,                  ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 4), ERR_SEV_MEDIUM, { uint16_t source; uint8_t channel; uint8_t kind; uint8_t response; int32_t value; }) /* a hardware fault and the response taken */ \
  X(ERR_PWRM_BAD_ARG,                ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 5), ERR_SEV_HIGH,   { int32_t value; int32_t min; int32_t max; })              /* a setting out of range */                                             \
  X(ERR_PWRM_NOT_RAIL,               ERR_TAG(ERR_MOD_SYS_POWER_MANAGER, 6), ERR_SEV_HIGH,   { uint16_t id; })                                      /* a matrix row whose target is neither the board nor a rail of it */

ERR_TAGS_DECLARE(ERR_SYS_POWER_MANAGER_MAP)
