/* The error policy (SYS_DEVICE.MD "Error policy"): which devices a reported chain names, how much each one's error
 * weighs there, whether its importance lets it through, and the response. */
#include <esp_log.h>
#include "sdkconfig.h"
#include "sys_device.h"

#define TAG __FILE_NAME__

#define WALK_MAX (CONFIG_ERRORS_POOL_BLOCKS + 1u)  // cycle guard, like err_log / err_record
#define HANDLED_MAX 16u                            // distinct devices handled per chain

static sys_device_action_runner_f s_run_action;
static sys_device_vm_stop_f s_vm_stop;

// Per task: a response that reports an error (a sweep's second failure) must not run the policy again.
static __thread bool s_in_handler;

void sys_device_set_vm_stop(sys_device_vm_stop_f fn) {
  s_vm_stop = fn;
}

void sys_device_set_action_runner(sys_device_action_runner_f fn) {
  s_run_action = fn;
}

// LOW lets CRITICAL through, MEDIUM also HIGH, HIGH also MEDIUM, CRITICAL also LOW. NONE nothing.
static bool admitted(uint8_t importance, uint8_t sev) {
  if (importance == SYS_DEVICE_IMPORTANCE_NONE || sev == ERR_SEV_NONE) {
    return false;
  }
  return sev == ERR_SEV_CRITICAL || (uint32_t)sev + importance > ERR_SEV_CRITICAL;
}

// Critical: the VM stopped (the program stops writing), then every device suspended (outputs safe).
static err_h system_fault(uint16_t id) {
  ESP_LOGE(TAG, "critical error (device %u): stopping the VM, suspending every device", (unsigned)id);
  err_h response = NULL;
  if (s_vm_stop != NULL) {
    err_h stop = s_vm_stop(id);
    if (stop != NULL) {
      response = ERR_WRAP(stop, ERR_DEV_FAULT_RESPONSE, .id = id, .sev = ERR_SEV_CRITICAL,
                          .stage = SYS_DEVICE_FAULT_STAGE_VM_STOP, .action = 0u);
    }
  }
  err_h err = sys_device_suspend_all();
  if (err != NULL) {
    err = ERR_WRAP(err, ERR_DEV_FAULT_RESPONSE, .id = id, .sev = ERR_SEV_CRITICAL,
                   .stage = SYS_DEVICE_FAULT_STAGE_SUSPEND, .action = 0u);
  }
  ERR_KEEP_FIRST(response, err);
  return response;
}

// An admitted error of device `id`: the system response first, then its action for that severity.
static err_h device_fault(uint16_t id, uint8_t sev, uint32_t action, err_h chain) {
  err_h response = sev == ERR_SEV_CRITICAL ? system_fault(id) : NULL;
  if (action == 0 || s_run_action == NULL) {
    return response;
  }
  err_h err = s_run_action(action, chain);
  if (err != NULL) {
    err = ERR_WRAP(err, ERR_DEV_FAULT_RESPONSE, .id = id, .sev = sev, .stage = SYS_DEVICE_FAULT_STAGE_ACTION,
                   .action = action);
  }
  ERR_KEEP_FIRST(response, err);
  return response;
}

static bool is_response_failure(err_h chain) {
  uint32_t left = WALK_MAX;
  for (err_h n = chain; n != NULL && left-- > 0; n = n->next) {
    if (n->tag == ERR_DEV_FAULT_RESPONSE) {
      return true;
    }
  }
  return false;
}

err_h sys_device_handle_error(err_h chain) {
  if (chain == NULL || s_in_handler || is_response_failure(chain)) {
    return NULL;
  }
  s_in_handler = true;

  err_h first = NULL;
  uint16_t handled[HANDLED_MAX];
  uint32_t handled_count = 0;
  bool system_done = false;
  uint32_t left = WALK_MAX;
  for (err_h n = chain; n != NULL && left-- > 0; n = n->next) {
    if (n->tag != ERR_DEV_ID) {
      // A critical failure above every device: nobody to attribute it to.
      if (handled_count == 0 && !system_done && err_sev(n) == ERR_SEV_CRITICAL) {
        system_done = true;
        ERR_KEEP_FIRST(first, system_fault(SYS_DEVICE_ID_NONE));
      }
      continue;
    }
    const uint16_t id = ERR_DATA(n, ERR_DEV_ID)->id;
    sys_device_error_policy_t p;
    const bool found = sys_device_error_policy_get(id, &p);
    if (found && p.importance == SYS_DEVICE_IMPORTANCE_NONE) {
      break;  // a device under test: its errors and every deeper cause are ignored (the chain is still logged)
    }
    bool seen = false;
    for (uint32_t i = 0; i < handled_count; i++) {
      seen = seen || handled[i] == id;
    }
    if (seen || handled_count == HANDLED_MAX) {
      continue;
    }
    handled[handled_count++] = id;
    // The error as this device sees it: from its node down (ERR_DEV_ID / ERR_DEV_OP add nothing, they are NONE).
    const uint8_t sev = err_chain_sev(n);
    // Gone (a failed CREATE): only a critical error gets a response, without an action.
    if (!(found ? admitted(p.importance, sev) : sev == ERR_SEV_CRITICAL)) {
      continue;
    }
    const uint32_t action = found ? p.actions[sev - ERR_SEV_LOW] : 0u;
    ERR_KEEP_FIRST(first, device_fault(id, sev, action, chain));
    system_done = system_done || sev == ERR_SEV_CRITICAL;
  }

  s_in_handler = false;
  return first;
}
