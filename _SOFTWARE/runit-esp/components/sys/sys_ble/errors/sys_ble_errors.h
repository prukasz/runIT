#pragma once

#include "errors.h"

#define ERR_SYS_BLE_MAP(X)                                                                                        \
  X(ERR_BLE_STACK,       ERR_TAG(ERR_MOD_SYS_BLE, 1),  ERR_SEV_HIGH,   { int32_t rc; }) /* NimBLE init / host call failed */ \
  X(ERR_BLE_ADV,         ERR_TAG(ERR_MOD_SYS_BLE, 2),  ERR_SEV_MEDIUM, { int32_t rc; }) /* advertising failed */   \
  X(ERR_BLE_GATT,        ERR_TAG(ERR_MOD_SYS_BLE, 3),  ERR_SEV_MEDIUM, { uint16_t uuid; int32_t rc; }) /* add / delete service failed */ \
  X(ERR_BLE_SVC_LOCKED,  ERR_TAG(ERR_MOD_SYS_BLE, 4),  ERR_SEV_LOW,    { uint16_t uuid; })                 \
  X(ERR_BLE_UUID_TAKEN,  ERR_TAG(ERR_MOD_SYS_BLE, 5),  ERR_SEV_LOW,    { uint16_t uuid; })                 \
  X(ERR_BLE_NOT_FOUND,   ERR_TAG(ERR_MOD_SYS_BLE, 6),  ERR_SEV_MEDIUM, { uint16_t uuid; }) /* no such service / characteristic */ \
  X(ERR_BLE_NO_MEM,      ERR_TAG(ERR_MOD_SYS_BLE, 7),  ERR_SEV_HIGH,   { uint16_t uuid; uint32_t size; })  \
  X(ERR_BLE_NO_TX,       ERR_TAG(ERR_MOD_SYS_BLE, 8),  ERR_SEV_MEDIUM, { uint16_t uuid; }) /* no TX buffer, or being removed */ \
  X(ERR_BLE_NO_RX,       ERR_TAG(ERR_MOD_SYS_BLE, 9),  ERR_SEV_MEDIUM, { uint16_t uuid; }) /* not writable / no RX buffer */ \
  X(ERR_BLE_TX_FULL,     ERR_TAG(ERR_MOD_SYS_BLE, 10), ERR_SEV_MEDIUM, { uint16_t uuid; uint16_t len; }) /* item dropped */ \
  X(ERR_BLE_RX_FULL,     ERR_TAG(ERR_MOD_SYS_BLE, 11), ERR_SEV_MEDIUM, { uint16_t uuid; uint16_t len; }) /* peer write dropped */ \
  X(ERR_BLE_TOO_LONG,    ERR_TAG(ERR_MOD_SYS_BLE, 12), ERR_SEV_MEDIUM, { uint16_t uuid; uint16_t len; uint16_t max; }) \
  X(ERR_BLE_BAD_ARG,     ERR_TAG(ERR_MOD_SYS_BLE, 13), ERR_SEV_HIGH,   {})                                 \
  X(ERR_BLE_FRAG,        ERR_TAG(ERR_MOD_SYS_BLE, 14), ERR_SEV_LOW,    { uint16_t uuid; uint8_t hdr; uint8_t expected; }) /* packet out of order: frame dropped */ \
  X(ERR_BLE_RX_HANDLER,  ERR_TAG(ERR_MOD_SYS_BLE, 15), ERR_SEV_MEDIUM, { uint16_t uuid; }) /* handler already set, or char has an RX buffer */

ERR_TAGS_DECLARE(ERR_SYS_BLE_MAP)
