#include "sys_device_policy.h"

#include "err_record.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "sys_device.h"
#include "sys_device_driver.h"
#include "sys_device_events.h"
#include "sys_actions.h"
#include "sys_system.h"
#include "router_queue_errors.h"
#include "utils.h"

#define TAG __FILE_NAME__

// A value of `actions[]` in this class is a response of the policy itself, not a record.
#define RESPONSE_CLASS 0xFFu

/* ----------------------------------------------------------------------------
 * The policies, by device ID. The error path reads one without the registry lock (an op holding it may wait for the
 * reporting task), so the table has a short critical section of its own.
 * ------------------------------------------------------------------------- */

static struct {
  struct {
    sys_device_error_policy_t policy;
    bool present;  // the device exists (CREATED, not yet REMOVED)
    uint8_t gen;   // the generation of that device (its CREATED): a REMOVED of another one is not its
  } e[SYS_DEVICE_COUNT];
} s_pol;  // zero at the start (.bss)

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* The chains that need a response, as records (err_record), from the task that reported them to the policy's own:
 * that task holds no lock of a device, so the response (a sweep, a device op) reaches every device, the failing one
 * included. */
U_RINGBUFFER_DEFINE(sys_device_policy_ring, CONFIG_SYS_DEVICE_POLICY_QUEUE_BYTES, RINGBUF_TYPE_NOSPLIT);

U_TASK_DEFINE(sys_device_policy_task_h, CONFIG_SYS_DEVICE_POLICY_TASK_STACK);

bool sys_device_policy_get(uint8_t id, sys_device_error_policy_t* out) {
  if (id >= SYS_DEVICE_COUNT) {
    return false;
  }
  taskENTER_CRITICAL(&s_mux);
  const bool present = s_pol.e[id].present;
  if (present) {
    *out = s_pol.e[id].policy;
  }
  taskEXIT_CRITICAL(&s_mux);
  return present;
}

static bool is_response(uint32_t action) {
  return (action >> 24) == RESPONSE_CLASS;
}

err_h sys_device_policy_set(uint8_t id, const sys_device_error_policy_t* p) {
  ERR_CHECK_RANGE(p->importance, SYS_DEVICE_IMPORTANCE_NONE, SYS_DEVICE_IMPORTANCE_CRITICAL);
  for (uint32_t i = 0; i < SYS_DEVICE_ERR_LEVELS; i++) {
    const uint32_t a = p->actions[i];
    ERR_CHECK(!is_response(a) || a == SYS_DEVICE_RESPONSE_SUSPEND || a == SYS_DEVICE_RESPONSE_RESET, ERR_BAD_ARG);
  }
  bool present = false;
  if (id < SYS_DEVICE_COUNT) {
    taskENTER_CRITICAL(&s_mux);
    present = s_pol.e[id].present;
    if (present) {
      s_pol.e[id].policy = *p;
    }
    taskEXIT_CRITICAL(&s_mux);
  }
  ERR_CHECK(present, ERR_DEV_NOT_FOUND, .id = id);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Following the registry: CREATED gives the default policy, REMOVED takes it away. Two tasks may publish the REMOVED
 * of a device and the CREATED of the next one with its ID in either order, so each carries the slot's generation: a
 * REMOVED counts only for the device whose CREATED was taken.
 * ------------------------------------------------------------------------- */

static err_h on_device(const sys_event_t* event, void* ctx) {
  (void)ctx;
  const uint16_t id = event->key.source;
  if (id >= SYS_DEVICE_COUNT) {
    return NULL;
  }
  const bool created = event->key.tag == SYS_DEVICE_EV_CREATED;
  const uint8_t gen = created ? SYS_EVENT_DATA(event, SYS_DEVICE_EV_CREATED)->gen
                              : SYS_EVENT_DATA(event, SYS_DEVICE_EV_REMOVED)->gen;
  taskENTER_CRITICAL(&s_mux);
  if (created) {
    s_pol.e[id].present = true;
    s_pol.e[id].gen = gen;
    s_pol.e[id].policy = (sys_device_error_policy_t){.importance = SYS_DEVICE_IMPORTANCE_LOW};
  } else if (s_pol.e[id].gen == gen) {
    s_pol.e[id].present = false;  // a REMOVED of an earlier device with this ID leaves the one that is there
  }
  taskEXIT_CRITICAL(&s_mux);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * The response to a reported chain: the policy's task
 * ------------------------------------------------------------------------- */

// LOW lets CRITICAL through, MEDIUM also HIGH, HIGH also MEDIUM, CRITICAL also LOW. NONE nothing.
static bool admitted(uint8_t importance, uint8_t sev) {
  if (importance == SYS_DEVICE_IMPORTANCE_NONE || sev == ERR_SEV_NONE) {
    return false;
  }
  return sev == ERR_SEV_CRITICAL || (uint32_t)sev + importance > ERR_SEV_CRITICAL;
}

// Critical: the board's safe state (SAFE stops the VM, then every device is suspended: outputs safe).
static err_h system_fault(uint8_t id) {
  ESP_LOGE(TAG, "critical error (device %u): the safe state", (unsigned)id);
  err_h err = sys_system_enter_safe_state(SYS_SAFE_FAULT, id);
  if (err != NULL) {
    err = ERR_WRAP(err, ERR_DEV_FAULT_RESPONSE, .id = id, .sev = ERR_SEV_CRITICAL,
                   .stage = SYS_DEVICE_FAULT_STAGE_SAFE, .action = 0u);
  }
  return err;
}

// The device's own response: the op on the device that failed, a write like any other.
static err_h run_response(uint8_t id, uint32_t action) {
  switch (action) {
    case SYS_DEVICE_RESPONSE_SUSPEND:
      return sys_device_write(id, CONTRACT_SYS_DEVICE_OP_SUSPEND, NULL, 0);
    case SYS_DEVICE_RESPONSE_RESET:
      return sys_device_write(id, CONTRACT_SYS_DEVICE_OP_RESET, NULL, 0);
    default:
      return ERR_NEW(ERR_BAD_ARG);
  }
}

/* A stored action (sys_actions), queued on the router. Not when the chain is that action's own failure (one of its
   steps, ERR_ACTION_STEP): the action would answer itself without end. */
static err_h run_action(uint32_t action, err_h chain) {
  uint32_t left = ERR_CHAIN_MAX;
  for (err_h n = chain; n != NULL && left-- > 0; n = err_cause(n)) {
    if (err_tag(n) == ERR_ACTION_STEP && ERR_DATA(n, ERR_ACTION_STEP)->id == action) {
      return NULL;
    }
  }
  return sys_actions_request(action, false);
}

// Runs the device's configured action (a built-in response or a stored action).
static err_h device_fault(uint8_t id, uint8_t sev, uint32_t action, err_h chain) {
  err_h err = is_response(action) ? run_response(id, action) : run_action(action, chain);
  if (err != NULL) {
    err = ERR_WRAP(err, ERR_DEV_FAULT_RESPONSE, .id = id, .sev = sev, .stage = SYS_DEVICE_FAULT_STAGE_ACTION,
                   .action = action);
  }
  return err;
}

static bool is_response_failure(err_h chain) {
  return err_find(chain, ERR_DEV_FAULT_RESPONSE) != NULL;
}

/* The policy on one chain. Walks from the outermost node: each device (ERR_DEV_ID) once, at its first mention, with
 * the severity from there down; a device at importance NONE ends the walk, and so does ERR_REMOTE (the rest is another
 * board's chain; the severity of a device above it still counts all of it). A critical node triggers system_fault()
 * at most once per chain. With `act` the responses run and the first failure is returned; without it nothing runs and
 * `*needed` says whether at least one real response would run (so no-op errors are never queued). */
static err_h walk(err_h chain, bool act, bool* needed) {
  err_h first = NULL;
  uint64_t handled = 0;  // bit i: device i handled already
  bool any_device = false;
  bool system_done = false;
  uint32_t left = ERR_CHAIN_MAX;
  *needed = false;
  for (err_h n = chain; n != NULL && left-- > 0 && (act || !*needed); n = err_cause(n)) {
    if (err_tag(n) == ERR_REMOTE) {
      break;  // below it the chain is another board's: its device IDs are not this board's
    }
    if (err_tag(n) != ERR_DEV_ID) {
      // A critical failure above every device: nobody to attribute it to.
      if (!any_device && !system_done && err_sev(n) == ERR_SEV_CRITICAL) {
        system_done = true;
        *needed = true;
        if (act) {
          ERR_KEEP_FIRST(first, system_fault(SYS_DEVICE_ID_NONE));
        }
      }
      continue;
    }
    const uint8_t id = ERR_DATA(n, ERR_DEV_ID)->id;
    sys_device_error_policy_t p;
    const bool found = sys_device_policy_get(id, &p);
    if (found && p.importance == SYS_DEVICE_IMPORTANCE_NONE) {
      break;  // a device under test: its errors and every deeper cause are ignored (the chain is still logged)
    }
    if (id < SYS_DEVICE_COUNT) {
      const uint64_t bit = 1ULL << id;
      if ((handled & bit) != 0) {
        continue;
      }
      handled |= bit;
    }
    any_device = true;
    // The error as this device sees it: from its node down (ERR_DEV_ID / ERR_DEV_OP add nothing, they are NONE).
    const uint8_t sev = err_chain_sev(n);
    // Gone (a failed CREATE): only a critical error gets a response, without an action.
    if (!(found ? admitted(p.importance, sev) : sev == ERR_SEV_CRITICAL)) {
      continue;
    }
    const uint32_t action = found ? p.actions[sev - ERR_SEV_LOW] : 0u;
    const bool do_sys = sev == ERR_SEV_CRITICAL && !system_done;
    const bool do_act = action != 0;
    if (!do_sys && !do_act) {
      continue;
    }
    *needed = true;
    if (act) {
      if (do_sys) {
        system_done = true;
        ERR_KEEP_FIRST(first, system_fault(id));
      }
      if (do_act) {
        ERR_KEEP_FIRST(first, device_fault(id, sev, action, chain));
      }
    }
  }
  return first;
}

/* The reporting task's side: no response runs here. The task may be inside a device op (it holds that device's lock),
 * so the chain goes to the policy's task as a record. A chain that needs no response is not queued. */
err_h sys_device_policy_handle(err_h chain) {
  if (chain == NULL || is_response_failure(chain) || xTaskGetCurrentTaskHandle() == sys_device_policy_task_h) {
    return NULL;  // a response's own failure, or an error reported while a response runs: no loops
  }
  bool needed = false;
  err_free(walk(chain, false, &needed));
  if (!needed) {
    return NULL;
  }
  uint8_t rec[ERR_RECORD_MAX];
  const uint32_t len = err_record(chain, rec, sizeof(rec));
  const bool queued = len > 0 && xRingbufferSend(sys_device_policy_ring, rec, len, NO_WAIT) == pdTRUE;
  ERR_CHECK(queued, ERR_DEV_FAULT_RESPONSE, .id = SYS_DEVICE_ID_NONE, .sev = err_chain_sev(chain),
            .stage = SYS_DEVICE_FAULT_STAGE_QUEUE, .action = 0u);
  return NULL;
}

/* The policy's task: a record becomes a chain again and the policy runs on it. A response's failure is reported from
 * here (it holds ERR_DEV_FAULT_RESPONSE and comes from this task, so it is not handled again). */
static void policy_task(void* arg) {
  (void)arg;
  while (1) {
    size_t size = 0;
    void* p = xRingbufferReceive(sys_device_policy_ring, &size, portMAX_DELAY);
    if (p == NULL) {
      continue;
    }
    uint8_t rec[ERR_RECORD_MAX];
    if (size > sizeof(rec)) {
      size = sizeof(rec);
    }
    memcpy(rec, p, size);  // a copy, so the ring's room is free again before the response runs
    vRingbufferReturnItem(sys_device_policy_ring, p);

    err_h chain = NULL;
    err_report(err_record_parse(rec, (uint32_t)size, &chain));  // a record cut short still gives the nodes it has
    bool needed = false;
    err_report(walk(chain, true, &needed));
    err_free(chain);
  }
}

// Before app_main: the device events it answers, and its task (static: stack and TCB in U_TASK_DEFINE).
__attribute__((constructor)) static void policy_start(void) {
  uint16_t handle;
  err_boot(sys_event_subscribe((sys_event_key_t){.tag = SYS_DEVICE_EV_CREATED, .source = SYS_EVENT_ANY_SOURCE,
                                                 .channel = SYS_EVENT_ANY_CHANNEL},
                               on_device, NULL, SYS_EVENT_RUN_INLINE, &handle));
  err_boot(sys_event_subscribe((sys_event_key_t){.tag = SYS_DEVICE_EV_REMOVED, .source = SYS_EVENT_ANY_SOURCE,
                                                 .channel = SYS_EVENT_ANY_CHANNEL},
                               on_device, NULL, SYS_EVENT_RUN_INLINE, &handle));
  U_TASK_START(sys_device_policy_task_h, policy_task, NULL, CONFIG_SYS_DEVICE_POLICY_TASK_PRIO);
}
