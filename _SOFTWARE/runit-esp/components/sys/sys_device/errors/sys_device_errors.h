#pragma once

#include "errors.h"
#include "sys_device_id.h"

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_SYS_DEVICE). Besides the registry's, the ones every
 * device raises the same way (suspended, range, pin), so they look the same
 * on all.
 * ------------------------------------------------------------------------- */

#define ERR_SYS_DEVICE_MAP(X, M)                                                                                                                                                                                                               \
  X(M, ERR_DEV_BAD_ID,         1,  ERR_SEV_HIGH,     { uint8_t id; }) /* SYS_DEVICE_ID_NONE */                                                                                                                                                 \
  X(M, ERR_DEV_NOT_FOUND,      2,  ERR_SEV_MEDIUM,   { uint8_t id; })                                                                                                                                                                          \
  X(M, ERR_DEV_ID_TAKEN,       3,  ERR_SEV_MEDIUM,   { uint8_t id; })                                                                                                                                                                          \
  X(M, ERR_DEV_TYPE_UNKNOWN,   4,  ERR_SEV_HIGH,     { uint16_t type; })                                                                                                                                                                       \
  X(M, ERR_DEV_SUSPENDED,      5,  ERR_SEV_MEDIUM,   { uint8_t id; })                                                                                                                                                                          \
  X(M, ERR_DEV_INSTALL,        6,  ERR_SEV_NONE,     { uint8_t id; uint16_t type; }) /* context: create failed */                                                                                                                              \
  X(M, ERR_DEV_ID,             7,  ERR_SEV_NONE,     { uint8_t id; }) /* context: the device of a call */                                                                                                                                      \
  X(M, ERR_DEV_BAD_PIN,        8,  ERR_SEV_HIGH,     { uint8_t id; uint8_t pin; })                                                                                                                                                             \
  X(M, ERR_DEV_OP,             9,  ERR_SEV_NONE,     { uint16_t op; uint8_t dir; }) /* context: route step */                                                                                                                                  \
  X(M, ERR_DEV_UNKNOWN_OP,     10, ERR_SEV_HIGH,     { uint16_t op; }) /* an op (contract) the device doesn't have */                                                                                                                          \
  X(M, ERR_DEV_PIN_LOCKED,     11, ERR_SEV_HIGH,     { uint8_t id; uint8_t pin; uint8_t owner; }) /* a write of a pin another device locked: only its owner writes it; `owner` is the device that locked it (SYS_DEVICE_ID_NONE: no device) */ \
  X(M, ERR_DEV_IN_USE,         12, ERR_SEV_MEDIUM,   { uint8_t id; uint64_t users; }) /* REMOVE of a device others use: bit i = device i */                                                                                                    \
  X(M, ERR_DEV_FAULT_RESPONSE, 13, ERR_SEV_HIGH,     { uint8_t id; uint8_t sev; uint8_t stage; uint32_t action; }) /* the policy's response to an error of device `id` failed (stage: sys_device_fault_stage_e); diagnostics only */           \
  X(M, ERR_DEV_SYSTEM,         14, ERR_SEV_MEDIUM,   { uint8_t id; }) /* REMOVE of a board device (SYS_DEVICE_CREATE_SYSTEM) */                                                                                                                \
  X(M, ERR_DEV_REMOTE_DOWN,    15, ERR_SEV_HIGH,     { uint8_t id; uint8_t board; }) /* a remote device whose board is gone, or no sys_device_remote on this board */                                                                          \
  X(M, ERR_DEV_LOCAL_OP,       16, ERR_SEV_HIGH,     { uint16_t op; }) /* an op the firmware calls only in C (SYS_DEVICE_OP_LOCAL), sent as a command or to a remote device */                                                                 \
  X(M, ERR_DEV_REENTRY,        17, ERR_SEV_HIGH,     { uint8_t id; }) /* a task took the lock of a device it holds already (the lock is not recursive) */                                                                                      \
  X(M, ERR_DEV_NOT_LOCKED,     18, ERR_SEV_HIGH,     { uint8_t id; }) /* a slot was changed without its lock */                                                                                                                                \
  X(M, ERR_DEV_BUSY,           19, ERR_SEV_MEDIUM,   { uint8_t id; uint16_t op; }) /* the lock of a device was not free in time: `op` is the op that holds it */                                                                               \
  X(M, ERR_DEV_CYCLE,          20, ERR_SEV_HIGH,     { uint8_t id; uint8_t dep; }) /* device `id` would use `dep`, which uses `id` already (itself or through others) */                                                                       \
  X(M, ERR_DEV_PIN_NOT_OWNED,  21, ERR_SEV_HIGH,     { uint8_t id; uint8_t pin; uint8_t owner; }) /* UNLOCK by a caller that did not lock the pin: `owner` did (SYS_DEVICE_ID_NONE: no device) */                                              \
  X(M, ERR_DEV_REMOTE_DEP,     22, ERR_SEV_LOW,      { uint8_t id; uint8_t dep; }) /* device `id` uses a remote device `dep`: allowed, its calls go over the way (latency, a WIRED wait under the lock of `id`) */

ERR_TAGS_DECLARE(MODULE_ID_SYS_DEVICE, ERR_SYS_DEVICE_MAP)
