#pragma once
#include <stdbool.h>
#include "sys_error.h"

// -----------------------------------------------------------------------------
// Fault hooks and device routing, registered at boot by the application
// (runit_error_wiring_init). sys_errors knows no module above it: anything it
// calls up is a registered function. An unregistered hook is skipped.
//
// Hooks borrow node/chain and return an owned response failure, or NULL. They
// must not retain the chain.
// -----------------------------------------------------------------------------

typedef err_h (*se_fault_hook_f)(err_h node, err_h chain);
typedef err_h (*se_device_report_f)(uint8_t device_id, se_level_e level, err_h error);
typedef bool (*se_device_ignored_f)(uint8_t device_id);

/**
 * @brief Hook for every node whose owner is in @p domain (owner & 0xFF00, for
 * example OWNER_SYS_BLE_BASE). Replaces an earlier hook for that domain.
 * @return ERR_BASE_NO_MEM when all CONFIG_SYS_ERRORS_MAX_DOMAIN_HOOKS slots are used.
 */
SE_MUST_USE err_h SE_register_domain_hook(uint16_t domain, se_fault_hook_f hook);

/** @brief Called once per chain for a CRITICAL node that no device response handled. */
void SE_register_system_hook(se_fault_hook_f hook);

/**
 * @brief Device routing: @p report answers a device's error at a level;
 * @p is_ignored stops the chain walk at a device whose errors are ignored.
 */
void SE_register_device_router(se_device_report_f report, se_device_ignored_f is_ignored);
