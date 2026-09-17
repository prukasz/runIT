#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_log.h"
#include "sys_actions.h"
#include "sys_device.h"
#include "sys_error.h"
#include "vm_exec.h"

#undef OWNER
#define OWNER OWNER_DEVICE_BASE

static inline err_h runit_invoke_action(uint8_t action_id) {
  if (action_id == 0) return NULL;
  err_h err = sys_actions_invoke(SYS_ACTION_SCOPE_STATIC, action_id);
  if (SE_IS_ERR(err) && err->tag == ERR_ACTION_NOT_FOUND) {
    return sys_actions_invoke(SYS_ACTION_SCOPE_DYNAMIC, action_id);
  }
  return err;
}

static inline err_h runit_fault_response_error(uint8_t device_id, sys_device_err_level_e level,
                                               sys_device_fault_stage_e stage, uint8_t action_id,
                                               uint16_t cause_tag) {
  SE_RET_ERR(ERR_DEV_FAULT_RESPONSE_FAILED, .dev_id = device_id, .level = (uint8_t)level,
             .stage = (uint8_t)stage, .action_id = action_id, .cause_tag = cause_tag);
}

/**
 * @brief Application coordinator error policy for classified device errors.
 *
 * Statically bound at link time, overriding sys_device's weak fallback.
 * For non-critical levels (LOW, MEDIUM, HIGH): invokes the configured action.
 * For CRITICAL level: latches root fault in VM, stops the VM, freezes all devices,
 * and invokes the configured critical action.
 */
err_h sys_device_app_error_policy(uint8_t device_id, sys_device_err_level_e level,
                                  uint8_t action_id, err_h error) {
  if (level != SYS_DEV_ERR_CRITICAL) {
    err_h action_error = runit_invoke_action(action_id);
    err_h root         = SE_get_error_root(action_error);
    return action_error ? runit_fault_response_error(device_id, level, SYS_DEV_FAULT_STAGE_ACTION,
                                                     action_id, root ? (uint16_t)root->tag : 0)
                        : NULL;
  }

  err_h root  = SE_get_error_root(error);
  bool  first = vm_exec_fault_latch(device_id, root ? root->owner : 0, root ? root->tag : ERR_DEP_FAILED);
  if (!first) return NULL;

  bool                     response_failed = false;
  sys_device_fault_stage_e failed_stage    = SYS_DEV_FAULT_STAGE_VM_STOP;
  uint16_t                 failed_tag      = 0;

  err_h stop_error = vm_exec_stop();
  if (stop_error) {
    err_h stop_root = SE_get_error_root(stop_error);
    response_failed = true;
    failed_stage    = SYS_DEV_FAULT_STAGE_VM_STOP;
    failed_tag      = stop_root ? (uint16_t)stop_root->tag : 0;
  }

  err_h first_error = sys_device_freeze_all();
  if (first_error && !response_failed) {
    err_h freeze_root = SE_get_error_root(first_error);
    response_failed   = true;
    failed_stage      = SYS_DEV_FAULT_STAGE_FREEZE;
    failed_tag        = freeze_root ? (uint16_t)freeze_root->tag : 0;
  }

  err_h action_error = runit_invoke_action(action_id);
  if (action_error && !response_failed) {
    err_h action_root = SE_get_error_root(action_error);
    response_failed   = true;
    failed_stage      = SYS_DEV_FAULT_STAGE_ACTION;
    failed_tag        = action_root ? (uint16_t)action_root->tag : 0;
  }

  return response_failed ? runit_fault_response_error(device_id, level, failed_stage, action_id, failed_tag)
                         : NULL;
}
