#include "sys_actions_static.h"
#include <stdbool.h>
#include <stdint.h>
#include "sys_actions.h"
#include "sys_device.h"
#include "sys_error.h"
#include "vm_exec.h"
#include "vm_loader.h"

#undef OWNER
#define OWNER OWNER_SYS_ACTIONS_BASE

// ---------------------------------------------------------
// Static (hardcoded) actions - moved from the removed sys_states component.
// These are what used to be a state's "base action"; now action ids
// 1-5's bound static functions, registered at boot.
// ---------------------------------------------------------

static err_h static_fn_freeze(void* arg) {
  (void)arg;
  return sys_device_freeze_all();
}

static err_h static_fn_resume(void* arg) {
  (void)arg;
  // Freeze and suspend are orthogonal in sys_device: only sync_all() clears a
  // freeze, only resume_all() clears a suspend. See SYS_DEVICE.MD.
  SE_RET_IF_ERR(sys_device_resume_all());
  SE_RET_IF_ERR(sys_device_sync_all());
  return NULL;
}

static err_h static_fn_suspend(void* arg) {
  (void)arg;
  return sys_device_suspend_all();
}

static err_h static_fn_reset(void* arg) {
  (void)arg;
  // Reset devices to initial configured state
  SE_RET_IF_ERR(sys_device_reset_all());

  // Reset VM execution from the start (rewind execution state and events/overrides/stats)
  SE_RET_IF_ERR(vm_exec_control(VM_EXEC_RESET_TO_START));

  // Re-run boot action 0 (re-applies baseline boot configuration / device settings)
  err_h boot_err = sys_actions_invoke(SYS_ACTION_ID_BOOT);
  if (SE_IS_ERR(boot_err) && boot_err->tag != ERR_ACTION_NOT_FOUND) {
    return boot_err;
  }
  return NULL;
}

static err_h static_fn_hard_reset(void* arg) {
  (void)arg;
  // Unload VM program completely and clear registries, arena, overrides, subscriptions & executor state
  SE_RET_IF_ERR(vm_loader_reset());

  // Uninstall and free every registered hardware device
  SE_RET_IF_ERR(sys_device_uninstall_all());

  // Restore baseline at boot config (re-creates onboard devices & boot setup via action 0)
  err_h boot_err = sys_actions_invoke(SYS_ACTION_ID_BOOT);
  if (SE_IS_ERR(boot_err) && boot_err->tag != ERR_ACTION_NOT_FOUND) {
    return boot_err;
  }
  return NULL;
}

void sys_actions_register_default_static(void) {
  sys_actions_bind_static(SYS_ACTION_ID_FREEZE, static_fn_freeze, NULL);
  sys_actions_bind_static(SYS_ACTION_ID_RESUME, static_fn_resume, NULL);
  sys_actions_bind_static(SYS_ACTION_ID_SUSPEND, static_fn_suspend, NULL);
  sys_actions_bind_static(SYS_ACTION_ID_RESET, static_fn_reset, NULL);
  sys_actions_bind_static(SYS_ACTION_ID_HARD_RESET, static_fn_hard_reset, NULL);
}

// ---------------------------------------------------------
// Device fault response error helper & application policy
// ---------------------------------------------------------

err_h sys_actions_fault_response_error(uint8_t device_id, sys_device_err_level_e level,
                                       sys_device_fault_stage_e stage, uint8_t action_id,
                                       uint16_t cause_tag) {
  SE_RET_ERR(ERR_DEV_FAULT_RESPONSE_FAILED,
             .dev_id = device_id, .level = (uint8_t)level, .stage = (uint8_t)stage,
             .action_id = action_id, .cause_tag = cause_tag);
}

err_h sys_actions_device_error_policy(uint8_t device_id,
                                      sys_device_err_level_e level,
                                      uint8_t action_id, err_h error) {
  if (level != SYS_DEV_ERR_CRITICAL) {
    err_h action_error = sys_actions_invoke(action_id);
    err_h root = SE_error_root(action_error);
    return action_error ? sys_actions_fault_response_error(device_id, level, SYS_DEV_FAULT_STAGE_ACTION,
                                                           action_id, root ? (uint16_t)root->tag : 0)
                        : NULL;
  }

  err_h root = SE_error_root(error);
  bool first = vm_exec_fault_latch(device_id, root ? root->owner : 0,
                                   root ? root->tag : ERR_DEP_FAILED);
  if (!first) return NULL;

  bool response_failed = false;
  sys_device_fault_stage_e failed_stage = SYS_DEV_FAULT_STAGE_CALLBACK;
  uint16_t failed_tag = 0;

  err_h stop_error = vm_exec_stop();
  if (stop_error) {
    err_h stop_root = SE_error_root(stop_error);
    response_failed = true;
    failed_stage = SYS_DEV_FAULT_STAGE_VM_STOP;
    failed_tag = stop_root ? (uint16_t)stop_root->tag : 0;
  }

  err_h first_error = sys_device_freeze_all();
  if (first_error && !response_failed) {
    err_h freeze_root = SE_error_root(first_error);
    response_failed = true;
    failed_stage = SYS_DEV_FAULT_STAGE_FREEZE;
    failed_tag = freeze_root ? (uint16_t)freeze_root->tag : 0;
  }

  err_h action_error = sys_actions_invoke(action_id);
  if (action_error && !response_failed) {
    err_h action_root = SE_error_root(action_error);
    response_failed = true;
    failed_stage = SYS_DEV_FAULT_STAGE_ACTION;
    failed_tag = action_root ? (uint16_t)action_root->tag : 0;
  }

  return response_failed ? sys_actions_fault_response_error(device_id, level, failed_stage,
                                                            action_id, failed_tag)
                         : NULL;
}

