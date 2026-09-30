#include "sys_actions_static.h"
#include <sdkconfig.h>
#include <stdbool.h>
#include <stdint.h>
#include "sys_actions.h"
#include "sys_device.h"
#include "sys_error.h"
#include "vm_exec.h"
#include "vm_loader.h"

#undef OWNER
#define OWNER OWNER_SYS_ACTIONS_STATIC

/** @brief Freeze all registered devices. */
static SE_MUST_USE err_h static_fn_freeze(void) {
  return sys_device_freeze_all();
}

/** @brief Resume and synchronize all registered devices. */
static SE_MUST_USE err_h static_fn_resume(void) {
  /** Freeze and suspend are orthogonal and require separate clearing calls. */
  SE_TRY(sys_device_resume_all());
  SE_TRY(sys_device_sync_all());
  return NULL;
}

/** @brief Suspend all registered devices. */
static SE_MUST_USE err_h static_fn_suspend(void) {
  return sys_device_suspend_all();
}

/** @brief Reset devices and VM execution, then restore the boot action. */
static SE_MUST_USE err_h static_fn_reset(void) {
  /** Reset devices to their initial configured state. */
  SE_TRY(sys_device_reset_all());

  /** Rewind VM execution state, events, overrides, and statistics. */
  SE_TRY(vm_exec_control(VM_EXEC_RESET_TO_START));

  /** Reapply baseline boot configuration through static action one. */
  err_h boot_err = sys_actions_invoke(SYS_ACTION_SCOPE_STATIC, CONFIG_SYS_ACTION_ID_BOOT);
  if (SE_IS_ERR(boot_err) && boot_err->tag != ERR_ACTION_NOT_FOUND) {
    return boot_err;
  }
  SE_release(boot_err);
  return NULL;
}

/** @brief Unload the VM and devices, then restore the boot action. */
static SE_MUST_USE err_h static_fn_hard_reset(void) {
  /** Clear the VM program, registries, arena, overrides, and subscriptions. */
  SE_TRY(vm_loader_reset());

  /** Uninstall and free every registered hardware device. */
  SE_TRY(sys_device_uninstall_all());

  /** Recreate baseline devices and boot setup through static action one. */
  err_h boot_err = sys_actions_invoke(SYS_ACTION_SCOPE_STATIC, CONFIG_SYS_ACTION_ID_BOOT);
  if (SE_IS_ERR(boot_err) && boot_err->tag != ERR_ACTION_NOT_FOUND) {
    return boot_err;
  }
  SE_release(boot_err);
  return NULL;
}

void sys_actions_register_static(void) {
  SE_release(sys_actions_bind_static(CONFIG_SYS_ACTION_ID_FREEZE, static_fn_freeze));
  SE_release(sys_actions_bind_static(CONFIG_SYS_ACTION_ID_RESUME, static_fn_resume));
  SE_release(sys_actions_bind_static(CONFIG_SYS_ACTION_ID_SUSPEND, static_fn_suspend));
  SE_release(sys_actions_bind_static(CONFIG_SYS_ACTION_ID_RESET, static_fn_reset));
  SE_release(sys_actions_bind_static(CONFIG_SYS_ACTION_ID_HARD_RESET, static_fn_hard_reset));
}
