#pragma once

#include <stdint.h>
#include "sys_actions.h"
#include "sys_device.h"

/**
 * @brief Default static action slot IDs (0..CONFIG_SYS_ACTIONS_STATIC_SLOTS-1).
 */
#define SYS_ACTION_ID_BOOT        0
#define SYS_ACTION_ID_FREEZE      1
#define SYS_ACTION_ID_RESUME      2
#define SYS_ACTION_ID_SUSPEND     3
#define SYS_ACTION_ID_RESET       4
#define SYS_ACTION_ID_HARD_RESET  5

/**
 * @brief Registers default static action functions onto action IDs 1-5.
 * Boot-only, not mutex-protected, register everything before concurrent access starts.
 */
void sys_actions_register_default_static(void);

/**
 * @brief Fault response error helper: wraps/creates ERR_DEV_FAULT_RESPONSE_FAILED error.
 */
err_h sys_actions_fault_response_error(uint8_t device_id, sys_device_err_level_e level,
                                       sys_device_fault_stage_e stage, uint8_t action_id,
                                       uint16_t cause_tag);

/**
 * @brief Application-level device error policy registered to sys_device.
 */
err_h sys_actions_device_error_policy(uint8_t device_id,
                                      sys_device_err_level_e level,
                                      uint8_t action_id, err_h error);
