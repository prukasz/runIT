#pragma once

#include "errors.h"

MODULE_ID_DECLARE(SYS_DEVICE, 0x20)  //@alias Devices @description Device registry and lifecycle contract and module ID
#define ERR_MOD_SYS_DEVICE MODULE_ID_SYS_DEVICE

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_DEVICE). Besides the registry's, the ones every
 * device raises the same way (suspended, range, pin), so they look the same
 * on all.
 * ------------------------------------------------------------------------- */

#define ERR_SYS_DEVICE_MAP(X)                                                                                     \
  X(ERR_DEV_BAD_ARG,        ERR_TAG(ERR_MOD_SYS_DEVICE, 1),  ERR_SEV_HIGH,   {})                                  \
  X(ERR_DEV_BAD_ID,         ERR_TAG(ERR_MOD_SYS_DEVICE, 2),  ERR_SEV_HIGH,   { uint8_t id; }) /* SYS_DEVICE_ID_NONE */ \
  X(ERR_DEV_NOT_FOUND,      ERR_TAG(ERR_MOD_SYS_DEVICE, 3),  ERR_SEV_MEDIUM, { uint8_t id; })                    \
  X(ERR_DEV_ID_TAKEN,       ERR_TAG(ERR_MOD_SYS_DEVICE, 4),  ERR_SEV_MEDIUM, { uint8_t id; })                    \
  X(ERR_DEV_TYPE_UNKNOWN,   ERR_TAG(ERR_MOD_SYS_DEVICE, 6),  ERR_SEV_HIGH,   { uint16_t type; })                   \
  X(ERR_DEV_SUSPENDED,      ERR_TAG(ERR_MOD_SYS_DEVICE, 10), ERR_SEV_MEDIUM, { uint8_t id; })                    \
  X(ERR_DEV_NO_MEM,         ERR_TAG(ERR_MOD_SYS_DEVICE, 11), ERR_SEV_CRITICAL, { uint8_t id; uint16_t size; })   \
  X(ERR_DEV_INSTALL,        ERR_TAG(ERR_MOD_SYS_DEVICE, 12), ERR_SEV_NONE,   { uint8_t id; uint16_t type; }) /* context: create failed */ \
  X(ERR_DEV_ID,             ERR_TAG(ERR_MOD_SYS_DEVICE, 13), ERR_SEV_NONE,   { uint8_t id; }) /* context: the device of a call */ \
  X(ERR_DEV_BAD_PIN,        ERR_TAG(ERR_MOD_SYS_DEVICE, 15), ERR_SEV_HIGH,   { uint8_t id; uint8_t pin; })       \
  X(ERR_DEV_RANGE,          ERR_TAG(ERR_MOD_SYS_DEVICE, 16), ERR_SEV_HIGH,   { uint8_t id; int32_t value; int32_t min; int32_t max; }) /* a value the chip would reject (signed: thresholds may be negative) */ \
  X(ERR_DEV_OP,             ERR_TAG(ERR_MOD_SYS_DEVICE, 17), ERR_SEV_NONE,   { uint16_t op; uint8_t dir; }) /* context: route step */ \
  X(ERR_DEV_UNKNOWN_OP,     ERR_TAG(ERR_MOD_SYS_DEVICE, 18), ERR_SEV_HIGH,   { uint16_t op; }) /* an op (contract) the device doesn't have */ \
  X(ERR_DEV_PIN_LOCKED,     ERR_TAG(ERR_MOD_SYS_DEVICE, 19), ERR_SEV_HIGH,   { uint8_t id; uint8_t pin; uint8_t owner; }) /* a write of a pin another device locked: only its owner writes it; `owner` is the device that locked it (SYS_DEVICE_ID_NONE: no device) */ \
  X(ERR_DEV_IN_USE,         ERR_TAG(ERR_MOD_SYS_DEVICE, 20), ERR_SEV_MEDIUM, { uint8_t id; uint64_t users; }) /* REMOVE of a device others use: bit i = device i */ \
  X(ERR_DEV_FAULT_RESPONSE, ERR_TAG(ERR_MOD_SYS_DEVICE, 22), ERR_SEV_HIGH,   { uint8_t id; uint8_t sev; uint8_t stage; uint32_t action; }) /* the policy's response to an error of device `id` failed (stage: sys_device_fault_stage_e); diagnostics only */ \
  X(ERR_DEV_SYSTEM,         ERR_TAG(ERR_MOD_SYS_DEVICE, 23), ERR_SEV_MEDIUM, { uint8_t id; }) /* REMOVE of a board device (SYS_DEVICE_CREATE_SYSTEM) */ \
  X(ERR_DEV_REMOTE_DOWN,    ERR_TAG(ERR_MOD_SYS_DEVICE, 24), ERR_SEV_HIGH,   { uint8_t id; uint8_t board; }) /* a remote device whose board is gone, or no sys_remote on this board */ \
  X(ERR_DEV_LOCAL_OP,       ERR_TAG(ERR_MOD_SYS_DEVICE, 25), ERR_SEV_HIGH,   { uint16_t op; }) /* an op the firmware calls only in C (SYS_DEVICE_OP_LOCAL), sent as a command or to a remote device */ \
  X(ERR_DEV_REENTRY,        ERR_TAG(ERR_MOD_SYS_DEVICE, 27), ERR_SEV_HIGH,   { uint8_t id; }) /* a task took the lock of a device it holds already (the lock is not recursive) */ \
  X(ERR_DEV_NOT_LOCKED,     ERR_TAG(ERR_MOD_SYS_DEVICE, 28), ERR_SEV_HIGH,   { uint8_t id; }) /* a slot was changed without its lock */ \
  X(ERR_DEV_BUSY,           ERR_TAG(ERR_MOD_SYS_DEVICE, 29), ERR_SEV_MEDIUM, { uint8_t id; uint16_t op; }) /* the lock of a device was not free in time: `op` is the op that holds it */ \
  X(ERR_DEV_CYCLE,          ERR_TAG(ERR_MOD_SYS_DEVICE, 30), ERR_SEV_HIGH,   { uint8_t id; uint8_t dep; }) /* device `id` would use `dep`, which uses `id` already (itself or through others) */ \
  X(ERR_DEV_PIN_NOT_OWNED,  ERR_TAG(ERR_MOD_SYS_DEVICE, 31), ERR_SEV_HIGH,   { uint8_t id; uint8_t pin; uint8_t owner; }) /* UNLOCK by a caller that did not lock the pin: `owner` did (SYS_DEVICE_ID_NONE: no device) */

ERR_TAGS_DECLARE(ERR_SYS_DEVICE_MAP)
