#pragma once

#include "errors.h"
#include "sys_ble_id.h"

// ERR_BLE_STACK: NimBLE's init or a host call; ERR_BLE_GATT: adding / deleting a service; ERR_BLE_FRAG: frame dropped.
#define ERR_SYS_BLE_MAP(X, M)                                                                                           \
  X(M, ERR_BLE_STACK,      1,  ERR_SEV_HIGH,   { int32_t rc; })                                                         \
  X(M, ERR_BLE_ADV,        2,  ERR_SEV_MEDIUM, { int32_t rc; })                                                         \
  X(M, ERR_BLE_GATT,       3,  ERR_SEV_MEDIUM, { uint16_t uuid; int32_t rc; })                                          \
  X(M, ERR_BLE_SVC_LOCKED, 4,  ERR_SEV_LOW,    { uint16_t uuid; })                                                      \
  X(M, ERR_BLE_UUID_TAKEN, 5,  ERR_SEV_LOW,    { uint16_t uuid; })                                                      \
  X(M, ERR_BLE_NOT_FOUND,  6,  ERR_SEV_MEDIUM, { uint16_t uuid; })                                                      \
  X(M, ERR_BLE_UUID,       7,  ERR_SEV_NONE,   { uint16_t uuid; }) /* context: BLE service or characteristic */         \
  X(M, ERR_BLE_NO_TX,      8,  ERR_SEV_MEDIUM, { uint16_t uuid; })                                                      \
  X(M, ERR_BLE_TX_FULL,    9,  ERR_SEV_MEDIUM, { uint16_t uuid; uint16_t len; })                                        \
  X(M, ERR_BLE_TOO_LONG,   10, ERR_SEV_MEDIUM, { uint16_t uuid; uint16_t len; uint16_t max; })                          \
  X(M, ERR_BLE_FRAG,       11, ERR_SEV_LOW,    { uint16_t uuid; uint8_t hdr; uint8_t want; })                           \
  X(M, ERR_BLE_HOOK,       12, ERR_SEV_MEDIUM, { uint16_t uuid; })

ERR_TAGS_DECLARE(MODULE_ID_BLE, ERR_SYS_BLE_MAP)
