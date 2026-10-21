/* The error policy (SYS_DEVICE.MD "Error policy"): which devices a reported chain names, how much each one's error
 * weighs there, whether its importance lets it through, and the response. */
#include <esp_log.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sys_device.h"
#include "sys_device_internal.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_DEVICE, "sys_device", ERR_SYS_DEVICE_MAP)

#define TAG __FILE_NAME__

#define WALK_MAX    (CONFIG_ERRORS_POOL_BLOCKS + 1u)  // cycle guard, like err_log / err_record
#define HANDLED_MAX 16u                            // distinct devices handled per chain

static struct {
  sys_device_action_runner_f run_action;
  sys_device_vm_stop_f vm_stop;
} s_hooks;

/* The policies, by device ID: one entry per registered instance (the registry adds and removes them with the
 * instance). The error path reads a policy without the registry lock (an op holding it may wait for the reporting
 * task), so the table has a short critical section of its own. */
static struct {
  struct {
    uint16_t id;
    sys_device_error_policy_t policy;
  } e[CONFIG_SYS_DEVICE_MAX];
  uint32_t count;
} s_policies;  // zero at the start (.bss)
static portMUX_TYPE s_policy_mux = portMUX_INITIALIZER_UNLOCKED;

// A value of `actions[]` in this class is a response of the policy itself, not a record.
#define RESPONSE_CLASS 0xFFu

static bool is_response(uint32_t action) {
  return (action >> 24) == RESPONSE_CLASS;
}

static bool response_known(uint32_t action) {
  return action == SYS_DEVICE_RESPONSE_SUSPEND || action == SYS_DEVICE_RESPONSE_RESET;
}

// Per task: a response that reports an error (a sweep's second failure) must not run the policy again.
static __thread bool s_in_handler;

// Index of `id` in the table, or s_policies.count (s_policy_mux held).
static uint32_t policy_find(uint16_t id) {
  for (uint32_t i = 0; i < s_policies.count; i++) {
    if (s_policies.e[i].id == id) {
      return i;
    }
  }
  return s_policies.count;
}

void sys_device_policy_add(uint16_t id) {
  taskENTER_CRITICAL(&s_policy_mux);
  if (s_policies.count < CONFIG_SYS_DEVICE_MAX) {  // as many as the registry's instances
    s_policies.e[s_policies.count].id = id;
    s_policies.e[s_policies.count].policy = (sys_device_error_policy_t){.importance = SYS_DEVICE_IMPORTANCE_LOW};
    s_policies.count++;
  }
  taskEXIT_CRITICAL(&s_policy_mux);
}

void sys_device_policy_remove(uint16_t id) {
  taskENTER_CRITICAL(&s_policy_mux);
  const uint32_t i = policy_find(id);
  if (i != s_policies.count) {
    memmove(&s_policies.e[i], &s_policies.e[i + 1], (s_policies.count - i - 1) * sizeof(s_policies.e[0]));
    s_policies.count--;
  }
  taskEXIT_CRITICAL(&s_policy_mux);
}

bool sys_device_error_policy_get(uint16_t id, sys_device_error_policy_t* out) {
  taskENTER_CRITICAL(&s_policy_mux);
  const uint32_t i = policy_find(id);
  const bool found = i != s_policies.count;
  if (found) {
    *out = s_policies.e[i].policy;
  }
  taskEXIT_CRITICAL(&s_policy_mux);
  return found;
}

// Any action ID (0 = none; it may be recorded later), importance NONE..CRITICAL.
err_h sys_device_policy_set(uint16_t id, const sys_device_error_policy_t* p) {
  SYS_DEV_CHECK_IN_RANGE(id, p->importance, SYS_DEVICE_IMPORTANCE_NONE, SYS_DEVICE_IMPORTANCE_CRITICAL);
  for (uint32_t i = 0; i < SYS_DEVICE_ERR_LEVELS; i++) {
    ERR_CHECK(!is_response(p->actions[i]) || response_known(p->actions[i]), ERR_DEV_BAD_ARG);
  }
  taskENTER_CRITICAL(&s_policy_mux);
  const uint32_t i = policy_find(id);
  const bool found = i != s_policies.count;
  if (found) {
    s_policies.e[i].policy = *p;
  }
  taskEXIT_CRITICAL(&s_policy_mux);
  ERR_CHECK(found, ERR_DEV_NOT_FOUND, .id = id);
  return NULL;
}

err_h sys_device_policy_reply(uint16_t id, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  sys_device_error_policy_t p;
  ERR_CHECK(sys_device_error_policy_get(id, &p), ERR_DEV_NOT_FOUND, .id = id);
  return cmd_reply(out, out_cap, out_len, &p, sizeof(p));
}

void sys_device_set_vm_stop(sys_device_vm_stop_f fn) {
  s_hooks.vm_stop = fn;
}

void sys_device_set_action_runner(sys_device_action_runner_f fn) {
  s_hooks.run_action = fn;
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
  if (s_hooks.vm_stop != NULL) {
    err_h stop = s_hooks.vm_stop(id);
    if (stop != NULL) {
      response = ERR_WRAP(stop, ERR_DEV_FAULT_RESPONSE, .id = id, .sev = ERR_SEV_CRITICAL,
                          .stage = SYS_DEVICE_FAULT_STAGE_VM_STOP, .action = 0u);
    }
  }
  err_h err = sys_device_sweep(SYS_DEVICE_OP_SUSPEND);
  if (err != NULL) {
    err = ERR_WRAP(err, ERR_DEV_FAULT_RESPONSE, .id = id, .sev = ERR_SEV_CRITICAL,
                   .stage = SYS_DEVICE_FAULT_STAGE_SUSPEND, .action = 0u);
  }
  ERR_KEEP_FIRST(response, err);
  return response;
}

// The device's own response: the op on the device that failed (a write like any other, so the registry lock is taken).
static err_h run_response(uint16_t id, uint32_t action) {
  switch (action) {
    case SYS_DEVICE_RESPONSE_SUSPEND:
      return sys_device_write(id, SYS_DEVICE_OP_SUSPEND, NULL, 0);
    case SYS_DEVICE_RESPONSE_RESET:
      return sys_device_write(id, SYS_DEVICE_OP_RESET, NULL, 0);
    default:
      return ERR_NEW(ERR_DEV_BAD_ARG);
  }
}

// An admitted error of device `id`: the system response first, then its action for that severity.
static err_h device_fault(uint16_t id, uint8_t sev, uint32_t action, err_h chain) {
  err_h response = sev == ERR_SEV_CRITICAL ? system_fault(id) : NULL;
  if (action == 0 || (!is_response(action) && s_hooks.run_action == NULL)) {
    return response;
  }
  err_h err = is_response(action) ? run_response(id, action) : s_hooks.run_action(action, chain);
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
