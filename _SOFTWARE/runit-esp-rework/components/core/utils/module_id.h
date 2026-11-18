#pragma once

#include <stdint.h>

/**
 * @file module_id.h
 * @brief One number per module, for everything the module is known by: its router key, the module of its errors, the
 * module of its events (EVT_TAG(module, code)) and, for a device contract, its contract ID. The number has one name,
 * MODULE_ID_<NAME>, and is written once: MODULE_ID_DECLARE in the module's own <module>_id.h. No list of modules exists and
 * no file includes them all. Two modules with one number fail to compile where both headers meet (the enumerator
 * MODULE_ID__0x20 declared twice), fail templates/sync.sh (generate_enums.py: the module_id group is unique) and fail at
 * start where both register (the error registry, the router's root table). The number is wire format (a router key,
 * the high half of an error tag and of an event tag): never renumber it.
 *   0x00           no module (the core errors: MODULE_ID_CORE, errors.h)
 *   0x01..0x1F     device contracts (IO, I2C, EVENT, PMON, VREG, PD, SUPPLY, HBRIDGE, ...)
 *   0x20..0xFE     the other modules
 *   0xFF           a device's own ops (SYS_DEVICE_CONTRACT_OWN): never a module
 * `id` is spelled as two uppercase hex digits and nothing after them.
 */

/**
 * @brief Gives a module its number: the enumerator `name` (MODULE_ID_<NAME>). In the module's own <module>_id.h
 * (in the component's folder; a power contract's in contracts/), which includes nothing but this file; every header
 * of the module that uses the number (errors, router, contract) includes it. Everything else takes the module by
 * that name: ERR_TAGS_DECLARE / ERR_MODULE_DEFINE, ROUTER_MODULE_REGISTER, SYS_EVENTS_DECLARE,
 * SYS_DEVICE_CONTRACT_DECLARE.
 * @usage MODULE_ID_DECLARE(MODULE_ID_PWRM, 0x22)  //@alias Power manager
 */
#define MODULE_ID_DECLARE(name, id)  \
  enum { MODULE_ID__##id };          \
  enum { name = (id) };              \
  _Static_assert((id) >= 0x01u && (id) <= 0xFEu, #name ": the module ID is not 0x01..0xFE");
