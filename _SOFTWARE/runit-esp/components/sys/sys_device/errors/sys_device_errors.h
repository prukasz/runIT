#pragma once

#include "errors.h"
#include "sys_device_id.h"

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_SYS_DEVICE). Besides the registry's, the ones every
 * device raises the same way (suspended, range, pin), so they look the same
 * on all.
 * ------------------------------------------------------------------------- */

#define ERR_SYS_DEVICE_MAP(X, M)                                                                                                                                                                                                               \
  X(M, ERR_DEV_BAD_ARG,        1,  ERR_SEV_HIGH,     {})                                                                                                                                                                                       \
  X(M, ERR_DEV_BAD_ID,         2,  ERR_SEV_HIGH,     { uint8_t id; }) /* SYS_DEVICE_ID_NONE */                                                                                                                                                 \
  X(M, ERR_DEV_NOT_FOUND,      3,  ERR_SEV_MEDIUM,   { uint8_t id; })                                                                                                                                                                          \
  X(M, ERR_DEV_ID_TAKEN,       4,  ERR_SEV_MEDIUM,   { uint8_t id; })                                                                                                                                                                          \
  X(M, ERR_DEV_TYPE_UNKNOWN,   6,  ERR_SEV_HIGH,     { uint16_t type; })                                                                                                                                                                       \
  X(M, ERR_DEV_SUSPENDED,      10, ERR_SEV_MEDIUM,   { uint8_t id; })                                                                                                                                                                          \
  X(M, ERR_DEV_NO_MEM,         11, ERR_SEV_CRITICAL, { uint8_t id; uint16_t size; })                                                                                                                                                           \
  X(M, ERR_DEV_INSTALL,        12, ERR_SEV_NONE,     { uint8_t id; uint16_t type; }) /* context: create failed */                                                                                                                              \
  X(M, ERR_DEV_ID,             13, ERR_SEV_NONE,     { uint8_t id; }) /* context: the device of a call */                                                                                                                                      \
  X(M, ERR_DEV_BAD_PIN,        15, ERR_SEV_HIGH,     { uint8_t id; uint8_t pin; })                                                                                                                                                             \
  X(M, ERR_DEV_RANGE,          16, ERR_SEV_HIGH,     { uint8_t id; int32_t value; int32_t min; int32_t max; }) /* a value the chip would reject (signed: thresholds may be negative) */                                                        \
  X(M, ERR_DEV_OP,             17, ERR_SEV_NONE,     { uint16_t op; uint8_t dir; }) /* context: route step */                                                                                                                                  \
  X(M, ERR_DEV_UNKNOWN_OP,     18, ERR_SEV_HIGH,     { uint16_t op; }) /* an op (contract) the device doesn't have */                                                                                                                          \
  X(M, ERR_DEV_PIN_LOCKED,     19, ERR_SEV_HIGH,     { uint8_t id; uint8_t pin; uint8_t owner; }) /* a write of a pin another device locked: only its owner writes it; `owner` is the device that locked it (SYS_DEVICE_ID_NONE: no device) */ \
  X(M, ERR_DEV_IN_USE,         20, ERR_SEV_MEDIUM,   { uint8_t id; uint64_t users; }) /* REMOVE of a device others use: bit i = device i */                                                                                                    \
  X(M, ERR_DEV_FAULT_RESPONSE, 22, ERR_SEV_HIGH,     { uint8_t id; uint8_t sev; uint8_t stage; uint32_t action; }) /* the policy's response to an error of device `id` failed (stage: sys_device_fault_stage_e); diagnostics only */           \
  X(M, ERR_DEV_SYSTEM,         23, ERR_SEV_MEDIUM,   { uint8_t id; }) /* REMOVE of a board device (SYS_DEVICE_CREATE_SYSTEM) */                                                                                                                \
  X(M, ERR_DEV_REMOTE_DOWN,    24, ERR_SEV_HIGH,     { uint8_t id; uint8_t board; }) /* a remote device whose board is gone, or no sys_device_remote on this board */                                                                                 \
  X(M, ERR_DEV_LOCAL_OP,       25, ERR_SEV_HIGH,     { uint16_t op; }) /* an op the firmware calls only in C (SYS_DEVICE_OP_LOCAL), sent as a command or to a remote device */                                                                 \
  X(M, ERR_DEV_REENTRY,        27, ERR_SEV_HIGH,     { uint8_t id; }) /* a task took the lock of a device it holds already (the lock is not recursive) */                                                                                      \
  X(M, ERR_DEV_NOT_LOCKED,     28, ERR_SEV_HIGH,     { uint8_t id; }) /* a slot was changed without its lock */                                                                                                                                \
  X(M, ERR_DEV_BUSY,           29, ERR_SEV_MEDIUM,   { uint8_t id; uint16_t op; }) /* the lock of a device was not free in time: `op` is the op that holds it */                                                                               \
  X(M, ERR_DEV_CYCLE,          30, ERR_SEV_HIGH,     { uint8_t id; uint8_t dep; }) /* device `id` would use `dep`, which uses `id` already (itself or through others) */                                                                       \
  X(M, ERR_DEV_PIN_NOT_OWNED,  31, ERR_SEV_HIGH,     { uint8_t id; uint8_t pin; uint8_t owner; }) /* UNLOCK by a caller that did not lock the pin: `owner` did (SYS_DEVICE_ID_NONE: no device) */                                              \
  X(M, ERR_DEV_REMOTE_DEP,     32, ERR_SEV_LOW,      { uint8_t id; uint8_t dep; }) /* device `id` uses a remote device `dep`: allowed, its calls go over the way (latency, a WIRED wait under the lock of `id`) */

ERR_TAGS_DECLARE(MODULE_ID_SYS_DEVICE, ERR_SYS_DEVICE_MAP)
