#pragma once

#include "errors.h"

/* The module's one number: the root key of its commands (router/) and the module of its errors. */
MODULE_ID_DECLARE(BLE, 0x2E)  //@alias Bluetooth Low Energy @description BLE GATT transport and configuration service module ID
#define ERR_MOD_SYS_BLE MODULE_ID_BLE

// ERR_BLE_STACK: NimBLE's init or a host call; ERR_BLE_GATT: adding / deleting a service; ERR_BLE_FRAG: frame dropped.
#define ERR_SYS_BLE_MAP(X)                                                                                             \
  X(ERR_BLE_STACK,      ERR_TAG(ERR_MOD_SYS_BLE, 1),  ERR_SEV_HIGH,   { int32_t rc; })                                 \
  X(ERR_BLE_ADV,        ERR_TAG(ERR_MOD_SYS_BLE, 2),  ERR_SEV_MEDIUM, { int32_t rc; })                                 \
  X(ERR_BLE_GATT,       ERR_TAG(ERR_MOD_SYS_BLE, 3),  ERR_SEV_MEDIUM, { uint16_t uuid; int32_t rc; })                  \
  X(ERR_BLE_SVC_LOCKED, ERR_TAG(ERR_MOD_SYS_BLE, 4),  ERR_SEV_LOW,    { uint16_t uuid; })                              \
  X(ERR_BLE_UUID_TAKEN, ERR_TAG(ERR_MOD_SYS_BLE, 5),  ERR_SEV_LOW,    { uint16_t uuid; })                              \
  X(ERR_BLE_NOT_FOUND,  ERR_TAG(ERR_MOD_SYS_BLE, 6),  ERR_SEV_MEDIUM, { uint16_t uuid; })                              \
  X(ERR_BLE_NO_MEM,     ERR_TAG(ERR_MOD_SYS_BLE, 7),  ERR_SEV_HIGH,   { uint16_t uuid; uint32_t size; })               \
  X(ERR_BLE_NO_TX,      ERR_TAG(ERR_MOD_SYS_BLE, 8),  ERR_SEV_MEDIUM, { uint16_t uuid; })                              \
  X(ERR_BLE_TX_FULL,    ERR_TAG(ERR_MOD_SYS_BLE, 9),  ERR_SEV_MEDIUM, { uint16_t uuid; uint16_t len; })               \
  X(ERR_BLE_TOO_LONG,   ERR_TAG(ERR_MOD_SYS_BLE, 10), ERR_SEV_MEDIUM, { uint16_t uuid; uint16_t len; uint16_t max; }) \
  X(ERR_BLE_FRAG,       ERR_TAG(ERR_MOD_SYS_BLE, 11), ERR_SEV_LOW,    { uint16_t uuid; uint8_t hdr; uint8_t want; })   \
  X(ERR_BLE_HOOK,       ERR_TAG(ERR_MOD_SYS_BLE, 12), ERR_SEV_MEDIUM, { uint16_t uuid; })

ERR_TAGS_DECLARE(ERR_SYS_BLE_MAP)
