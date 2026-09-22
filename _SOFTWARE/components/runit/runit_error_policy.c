/*
 * Error policy of the application and the wiring of every upward call the
 * error system makes: device routing, device policy, system fault, domain
 * hooks, action executor and VM callback route. Registered once, as the first
 * boot step after SE_init(), so no error is handled without a policy.
 */
#include "runit_error_policy.h"
#include <sdkconfig.h>
#include "sys_actions.h"
#include "sys_ble.h"
#include "sys_event.h"
#include "sys_device.h"
#include "sys_error_hooks.h"
#include "sys_error_log.h"
#include "sys_interface.h"
#include "vm_event.h"
#include "vm_exec.h"

#undef OWNER
#define OWNER OWNER_RUNIT_ERROR_POLICY

// Invoke action by scope and ID. Zero ID disables the action.
static SE_MUST_USE err_h runit_invoke_action(uint8_t scope, uint8_t action_id) {
  if (action_id == 0) return NULL;
  return sys_actions_invoke(scope, action_id);
}

static SE_MUST_USE err_h runit_response_error(uint8_t id, se_level_e level, sys_device_fault_stage_e stage, uint8_t action, err_h cause) {
  if (!cause) return NULL;
  err_h root = SE_get_error_root(cause);
  return SE_WRAP_ERR(cause, ERR_DEV_FAULT_RESPONSE_FAILED, .dev_id = id, .level = level, .stage = stage, .action_id = action, .cause_tag = root ? root->tag : 0);
}

static SE_MUST_USE err_h runit_system_fault(uint8_t id, err_h node) {
  // Preserve the triggering node's severity/context rather than reclassifying
  // an unrelated leaf. Global shutdown is latched, device actions are separate.
  if (!vm_exec_fault_latch(id, node->owner, node->tag)) return NULL;
  err_h response = runit_response_error(id, SE_LEVEL_CRITICAL, SYS_DEV_FAULT_STAGE_VM_STOP, 0, vm_exec_stop());
  err_h suspend  = runit_response_error(id, SE_LEVEL_CRITICAL, SYS_DEV_FAULT_STAGE_SUSPEND, 0, sys_device_suspend_all());
  if (!response) return suspend;
  if (suspend) {
    // Both stages failed: return the VM-stop failure, keep the suspend one in diagnostics.
    SE_log(suspend);
  }
  return response;
}

static SE_MUST_USE err_h runit_system_hook(err_h node, err_h chain) {
  (void)chain;
  // 255 is diagnostic metadata only; never used to look up a device policy.
  return runit_system_fault(UINT8_MAX, node);
}

static SE_MUST_USE err_h runit_device_policy(uint8_t device_id, se_level_e level, uint8_t action_scope, uint8_t action_id, err_h error) {
  err_h response = level == SE_LEVEL_CRITICAL ? runit_system_fault(device_id, error) : NULL;
  err_h action   = runit_response_error(device_id, level, SYS_DEV_FAULT_STAGE_ACTION, action_id, runit_invoke_action(action_scope, action_id));
  if (!response) return action;
  SE_release(action);
  return response;
}

err_h runit_error_wiring_init(void) {
  SE_register_device_router(sys_device_report_error_with_level, sys_device_is_ignored);
  sys_device_register_error_policy(runit_device_policy);
  SE_register_system_hook(runit_system_hook);
  SE_TRY(SE_register_domain_hook(OWNER_SYS_BLE_BASE, sys_ble_handle_fault));
  SE_TRY(SE_register_domain_hook(OWNER_SYS_INTERFACE_BASE, sys_interface_handle_fault));
  SE_TRY(SE_register_domain_hook(OWNER_SYS_ACTIONS_BASE, sys_actions_handle_fault));
  SE_TRY(SE_register_domain_hook(OWNER_VM_BASE, sys_vm_handle_fault));
  sys_event_register_action_executor(sys_actions_invoke);
  SE_TRY(sys_event_register_route(CONFIG_SYS_EVENT_ROUTE_VM, vm_event_route));
  return NULL;
}
