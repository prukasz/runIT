#pragma once

#include <stdint.h>

/**
 * @file module_id.h
 * @brief One number per module, for everything the module is known by: its router key, the module of its errors
 * (ERR_MOD_*), the module of its events (EVT_TAG(module, code)) and, for a device contract, its contract ID. A module
 * declares it once in its own header and claims it once in one source of its component; no list of modules exists
 * and no file includes them all. Two modules with one number fail to compile where both headers meet (the enumerator
 * MODULE_ID__0x20 declared twice) and fail to link otherwise (the global symbol module_id__0x20 defined twice). The
 * number is wire format (a router key, the high half of an error tag and of an event tag): never renumber it.
 *   0x00           no module
 *   0x01..0x1F     device contracts (IO, I2C, EVENT, PMON, VREG, PD, SUPPLY, HBRIDGE, ...)
 *   0x20..0xFE     the other modules
 *   0xFF           a device's own ops (SYS_DEVICE_CONTRACT_OWN): never a module
 * `id` is spelled as two uppercase hex digits and nothing after them, the same in the declaration and the claim.
 */

/**
 * @brief Gives module NAME its number: MODULE_ID_<NAME>. In the module's own header, above everything that uses it (a
 * module with errors: above its error table, in the errors header).
 * @usage MODULE_ID_DECLARE(PWRM, 0x22)  // MODULE_ID_PWRM; #define ERR_MOD_SYS_POWER_MANAGER MODULE_ID_PWRM
 */
#define MODULE_ID_DECLARE(name, id)                                                       \
  enum { MODULE_ID__##id };                                                               \
  enum { MODULE_ID_##name = (id) };                                                       \
  _Static_assert((id) >= 0x01u && (id) <= 0xFEu, #name ": the module ID is not 0x01..0xFE");

/**
 * @brief Claims the number of module NAME for the whole firmware: once, in one source of the module's component.
 * @usage MODULE_ID_DEFINE(PWRM, 0x22)  // sys_power_manager.c
 */
#define MODULE_ID_DEFINE(name, id)                                                         \
  _Static_assert(MODULE_ID_##name == (id), #name ": the ID differs from its MODULE_ID_DECLARE"); \
  __attribute__((used)) const uint8_t module_id__##id = 0;
