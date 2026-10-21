#pragma once

/* Private to this component: what the registry (sys_device.c) asks of the error policy (sys_device_policy.c), which
 * keeps the policies by device ID. */

#include "sys_device.h"

/** @brief A new instance: its policy starts at LOW with no actions. Registry lock held. */
void sys_device_policy_add(uint16_t id);

/** @brief The instance is gone: its policy is forgotten. Registry lock held. */
void sys_device_policy_remove(uint16_t id);

/** @brief ERROR_POLICY write (ERR_DEV_RANGE for an importance past CRITICAL). */
err_h sys_device_policy_set(uint16_t id, const sys_device_error_policy_t* p);

/** @brief ERROR_POLICY read: answers the policy. */
err_h sys_device_policy_reply(uint16_t id, uint8_t* out, uint32_t out_cap, uint32_t* out_len);
