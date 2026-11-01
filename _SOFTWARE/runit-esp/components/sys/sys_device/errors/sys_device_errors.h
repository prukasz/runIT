#pragma once

#include "errors.h"

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_DEVICE). Besides the registry's, the ones every
 * device raises the same way (suspended, range, pin), so they look the same
 * on all.
 * ------------------------------------------------------------------------- */

#define ERR_SYS_DEVICE_MAP(X)                                                                                     \
  X(ERR_DEV_BAD_ARG,        ERR_TAG(ERR_MOD_SYS_DEVICE, 1),  ERR_SEV_HIGH,   {})                                  \
  X(ERR_DEV_BAD_ID,         ERR_TAG(ERR_MOD_SYS_DEVICE, 2),  ERR_SEV_HIGH,   { uint16_t id; }) /* SYS_DEVICE_ID_NONE */ \
  X(ERR_DEV_NOT_FOUND,      ERR_TAG(ERR_MOD_SYS_DEVICE, 3),  ERR_SEV_MEDIUM, { uint16_t id; })                    \
  X(ERR_DEV_ID_TAKEN,       ERR_TAG(ERR_MOD_SYS_DEVICE, 4),  ERR_SEV_MEDIUM, { uint16_t id; })                    \
  X(ERR_DEV_TABLE_FULL,     ERR_TAG(ERR_MOD_SYS_DEVICE, 5),  ERR_SEV_HIGH,   { uint16_t id; }) /* CONFIG_SYS_DEVICE_MAX */ \
  X(ERR_DEV_TYPE_UNKNOWN,   ERR_TAG(ERR_MOD_SYS_DEVICE, 6),  ERR_SEV_HIGH,   { uint16_t type; })                   \
  X(ERR_DEV_SUSPENDED,      ERR_TAG(ERR_MOD_SYS_DEVICE, 10), ERR_SEV_MEDIUM, { uint16_t id; })                    \
  X(ERR_DEV_NO_MEM,         ERR_TAG(ERR_MOD_SYS_DEVICE, 11), ERR_SEV_CRITICAL, { uint16_t id; uint16_t size; })   \
  X(ERR_DEV_INSTALL,        ERR_TAG(ERR_MOD_SYS_DEVICE, 12), ERR_SEV_NONE,   { uint16_t id; uint16_t type; }) /* context: create failed */ \
  X(ERR_DEV_ID,             ERR_TAG(ERR_MOD_SYS_DEVICE, 13), ERR_SEV_NONE,   { uint16_t id; }) /* context: route step / sweep */ \
  X(ERR_DEV_BAD_PIN,        ERR_TAG(ERR_MOD_SYS_DEVICE, 15), ERR_SEV_HIGH,   { uint16_t id; uint8_t pin; })       \
  X(ERR_DEV_RANGE,          ERR_TAG(ERR_MOD_SYS_DEVICE, 16), ERR_SEV_HIGH,   { uint16_t id; int32_t value; int32_t min; int32_t max; }) /* a value the chip would reject (signed: thresholds may be negative) */ \
  X(ERR_DEV_OP,             ERR_TAG(ERR_MOD_SYS_DEVICE, 17), ERR_SEV_NONE,   { uint16_t op; uint8_t dir; }) /* context: route step */ \
  X(ERR_DEV_UNKNOWN_OP,     ERR_TAG(ERR_MOD_SYS_DEVICE, 18), ERR_SEV_HIGH,   { uint16_t op; }) /* an op (contract) the device doesn't have */ \
  X(ERR_DEV_PIN_LOCKED,     ERR_TAG(ERR_MOD_SYS_DEVICE, 19), ERR_SEV_HIGH,   { uint16_t id; uint8_t pin; }) /* a locked pin: only its owner drives it (sys_pins.h) */ \
  X(ERR_DEV_IN_USE,         ERR_TAG(ERR_MOD_SYS_DEVICE, 20), ERR_SEV_MEDIUM, { uint16_t id; uint16_t user; }) /* REMOVE of a device another one uses */ \
  X(ERR_DEV_DEPS_FULL,      ERR_TAG(ERR_MOD_SYS_DEVICE, 21), ERR_SEV_HIGH,   { uint16_t id; }) /* CONFIG_SYS_DEVICE_MAX_DEPS */ \
  X(ERR_DEV_FAULT_RESPONSE, ERR_TAG(ERR_MOD_SYS_DEVICE, 22), ERR_SEV_HIGH,   { uint16_t id; uint8_t sev; uint8_t stage; uint32_t action; }) /* the policy's response to an error of device `id` failed (stage: sys_device_fault_stage_e); diagnostics only */ \
  X(ERR_DEV_SYSTEM,         ERR_TAG(ERR_MOD_SYS_DEVICE, 23), ERR_SEV_MEDIUM, { uint16_t id; }) /* REMOVE of a board device (sys_device_mark_system) */ \
  X(ERR_DEV_REMOTE_DOWN,    ERR_TAG(ERR_MOD_SYS_DEVICE, 24), ERR_SEV_HIGH,   { uint16_t id; uint8_t board; }) /* a remote device whose board is gone, or no sys_remote on this board */ \
  X(ERR_DEV_REMOTE_NOT_WIRED, ERR_TAG(ERR_MOD_SYS_DEVICE, 25), ERR_SEV_MEDIUM, { uint16_t id; uint8_t board; }) /* a blocking remote device whose board is no wired neighbour */

ERR_TAGS_DECLARE(ERR_SYS_DEVICE_MAP)
