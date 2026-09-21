#pragma once
#include "sys_actions.h"
#include "sys_device.h"
#include "sys_error.h"
#include "vm_exec.h"

#undef OWNER
#define OWNER OWNER_DEVICE_BASE

// Invoke action by scope and ID. Zero ID disables the action.
static err_h runit_invoke_action(uint8_t scope, uint8_t action_id) {
  if (action_id == 0) return NULL;
  return sys_actions_invoke(scope, action_id);
}

static err_h runit_response_error(uint8_t id, sys_device_err_level_e level, sys_device_fault_stage_e stage, uint8_t action, err_h cause) {
  if (!cause) return NULL;
  err_h root = SE_get_error_root(cause);
  return SE_WRAP_ERR(cause, ERR_DEV_FAULT_RESPONSE_FAILED, .dev_id = id, .level = level, .stage = stage, .action_id = action, .cause_tag = root ? root->tag : 0);
}

static err_h runit_system_fault(uint8_t id, err_h node) {
  // Preserve the triggering node's severity/context rather than reclassifying
  // an unrelated leaf. Global shutdown is latched, device actions are separate.
  if (!vm_exec_fault_latch(id, node->owner, node->tag)) return NULL;
  err_h response = runit_response_error(id, SYS_DEV_ERR_CRITICAL, SYS_DEV_FAULT_STAGE_VM_STOP, 0, vm_exec_stop());
  err_h freeze   = runit_response_error(id, SYS_DEV_ERR_CRITICAL, SYS_DEV_FAULT_STAGE_FREEZE, 0, sys_device_freeze_all());
  if (!response) return freeze;
  SE_release(freeze);
  return response;
}

err_h sys_system_handle_fault(err_h node, err_h chain) {
  (void)chain;
  // 255 is diagnostic metadata only; never used to look up a device policy.
  return runit_system_fault(UINT8_MAX, node);
}

err_h sys_device_app_error_policy(uint8_t device_id, sys_device_err_level_e level, uint8_t action_scope, uint8_t action_id, err_h error) {
  err_h response = level == SYS_DEV_ERR_CRITICAL ? runit_system_fault(device_id, error) : NULL;
  err_h action   = runit_response_error(device_id, level, SYS_DEV_FAULT_STAGE_ACTION, action_id, runit_invoke_action(action_scope, action_id));
  if (!response) return action;
  SE_release(action);
  return response;
}
