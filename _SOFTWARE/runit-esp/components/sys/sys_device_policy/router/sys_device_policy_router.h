#pragma once

#include <stdint.h>

#include "sys_device_policy_id.h"
#include "router_table.h"
#include "sys_device_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under the root key MODULE_ID_POLICY (packed, little-endian). Never renumber. The router registers itself
 * (ROUTER_MODULE_REGISTER): no component names it. */

/** @brief POLICY read key: the device */
typedef struct __attribute__((packed)) {
  uint8_t id;  /**< Device ID */
} sys_device_policy_key_t;

/** @brief POLICY write args and read answer: the device and its error policy */
typedef struct __attribute__((packed)) {
  uint8_t id;                        /**< Device ID */
  sys_device_error_policy_t policy;  /**< Its error policy */
} sys_device_policy_entry_t;

/**
 * @brief The table, once: X(P, OP_NAME, op code, key_t, value_t, flags) (cmd_table.h). POLICY RW: the read takes the
 * device and answers its ID and policy, the write takes the same (the mirror).
 */
#define POLICY_OPERATIONS(X, P) \
  X(P, OP_POLICY, 0x01, sys_device_policy_key_t, sys_device_policy_entry_t, 0)

CMD_TABLE_DECLARE(POLICY)

/**
 * @brief The device error policy, under the root key MODULE_ID_POLICY.
 */
ROUTER_MODULE_DECLARE(sys_device_policy_router);

#ifdef __cplusplus
}
#endif
