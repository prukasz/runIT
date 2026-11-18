#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "sys_device.h"
#include "sys_device_events.h"
#include "sys_device_hooks.h"
#include "sys_device_registry_internal.h"
#include "sys_device_remote_internal.h"
#include "sys_device_slots_internal.h"

/* ----------------------------------------------------------------------------
 * The task's calls: who calls whom, and the events held until its locks are let go
 * ------------------------------------------------------------------------- */

/* Per task. A call made inside an op (a driver calling the device it uses) is nested in the call of that op: the task
 * then holds the lock of every slot on the way. `s_frame` points to the innermost call frame on the task's stack,
 * linked outward by `up` (NULL outside any call; a zeroed `.tbss` pointer). */
typedef struct call_frame_s {
  const struct call_frame_s* up;  // enclosing call frame, or NULL at outermost
  uint8_t id;                     // device whose op runs on this frame, or SYS_DEVICE_ID_NONE if invalid
} call_frame_t;

static __thread const call_frame_t* s_frame;

uint8_t sys_device_caller(void) {
  return (s_frame != NULL && s_frame->up != NULL) ? s_frame->up->id : SYS_DEVICE_ID_NONE;
}

uint8_t sys_device_self(void) {
  return s_frame != NULL ? s_frame->id : SYS_DEVICE_ID_NONE;
}

bool sys_device_self_suspended(void) {
  return s_frame != NULL && s_frame->id < SYS_DEVICE_COUNT && sys_device_slot_is_suspended(s_frame->id);
}

/* The safe-state gate: the SUSPEND sweeps running now, on any task. While it is not 0 a write that comes from no
 * device and a CREATE are refused on every device, so nothing starts an output the sweep has not reached yet. A
 * device's own calls pass: a suspend() hook reaches the devices it uses, which are suspended after it. */
static volatile uint32_t s_safe_gate;
static portMUX_TYPE s_gate_lock = portMUX_INITIALIZER_UNLOCKED;

static void safe_gate(bool enter) {
  portENTER_CRITICAL(&s_gate_lock);
  s_safe_gate += enter ? 1u : (uint32_t)-1;
  portEXIT_CRITICAL(&s_gate_lock);
}

// Whether the call this task makes now is one the safe-state gate refuses: it comes from no device.
static bool gated(void) {
  return s_safe_gate != 0 && sys_device_caller() == SYS_DEVICE_ID_NONE;
}

/* ----------------------------------------------------------------------------
 * Interceptors: set at boot, read without a lock afterwards
 * ------------------------------------------------------------------------- */

/* The most bytes an interceptor's `out` holds: the command it passes on (a value clamped to a limit). On the caller's
 * stack, only while the call is intercepted. */
#define INTERCEPT_MAX 32u

typedef struct {
  uint8_t contract;
  sys_device_interceptor_f fn;       // before the device
  sys_device_intercept_done_f done;  // after it; NULL: none
} interceptor_t;

static interceptor_t s_interceptors[CONFIG_SYS_DEVICE_INTERCEPTORS];
static uint32_t s_interceptor_count;

err_h sys_device_set_interceptor(uint8_t contract, sys_device_interceptor_f fn, sys_device_intercept_done_f done) {
  ERR_CHECK(fn != NULL && s_interceptor_count < CONFIG_SYS_DEVICE_INTERCEPTORS, ERR_DEV_BAD_ARG);
  for (uint32_t i = 0; i < s_interceptor_count; i++) {
    ERR_CHECK(s_interceptors[i].contract != contract, ERR_DEV_BAD_ARG);  // one per contract
  }
  s_interceptors[s_interceptor_count++] = (interceptor_t){.contract = contract, .fn = fn, .done = done};
  return NULL;
}

static const interceptor_t* find_interceptor(uint16_t op) {
  for (uint32_t i = 0; i < s_interceptor_count; i++) {
    if (s_interceptors[i].contract == (uint8_t)(op >> 8)) {
      return &s_interceptors[i];
    }
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * A call: caller -> interceptor -> device -> caller, then the interceptor's `done`
 * ------------------------------------------------------------------------- */

/** @brief How a call is made: by C, as a command, as a device's step (sys_device_step), as a delivery, or in a sweep. */
typedef struct {
  bool routed;  /**< A command: a LOCAL op is refused on every device */
  bool step;    /**< A step: try-lock only and skip when suspended or gated */
  bool sweep;   /**< From sys_device_sweep(): tell only of CREATED/REMOVED per device */
  uint8_t gen;  /**< Generation check (step or deliver): 0 for any other call */
} how_t;

/** @brief A NOLOCK op checked under the lock and counted in flight: the caller runs it after the lock. */
typedef struct {
  cmd_f fn;        /**< The handler; NULL: nothing left to run */
  void* instance;  /**< The driver's struct, kept alive by the count (REMOVE drains it) */
} unlocked_t;

/* The device's stage, with the lock of its slot held. CREATE / CREATE_REMOTE make the slot a device; a registry op
 * runs on the device's ID (any state); a type's op on the driver's struct, a write only while not suspended (ALWAYS
 * aside) and not refused by the safe-state gate. A LOCAL op (its args carry a pointer) is refused to a command
 * (`routed`) and to a remote device. A remote device's type op is checked here and not run: `*remote` gets where it
 * goes, the caller sends it. A NOLOCK op is checked here, counted in flight and not run: `*later` gets it, the caller
 * runs it without the lock. */
static err_h run_locked(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out, bool routed,
                        sys_device_remote_t* remote, unlocked_t* later) {
  if (op == CONTRACT_SYS_DEVICE_OP_CREATE || op == CONTRACT_SYS_DEVICE_OP_CREATE_REMOTE) {
    ERR_CHECK(dir == CMD_DIR_WRITE, ERR_DEV_UNKNOWN_OP, .op = op);
    ERR_CHECK(!gated(), ERR_DEV_SUSPENDED, .id = id);  // no new device while every device goes into the safe state
    return op == CONTRACT_SYS_DEVICE_OP_CREATE ? sys_device_registry_create(id, in) : sys_device_remote_create(id, in);
  }
  ERR_CHECK(sys_device_slot_is_installed(id), ERR_DEV_NOT_FOUND, .id = id);
  const sys_device_op_t* own = sys_device_registry_op(op);
  const sys_device_op_t* row = own != NULL ? own : sys_device_type_op(id, op);
  const cmd_f fn = sys_device_op_side(row, dir);
  ERR_CHECK(fn != NULL, ERR_DEV_UNKNOWN_OP, .op = op);
  if (own != NULL) {
    return fn((void*)(uintptr_t)id, in, out);
  }
  const bool refused = sys_device_slot_is_suspended(id) || gated();
  ERR_CHECK(dir == CMD_DIR_READ || (row->flags & SYS_DEVICE_OP_ALWAYS) || !refused, ERR_DEV_SUSPENDED, .id = id);
  const bool is_remote = sys_device_slot_is_remote(id);
  ERR_CHECK(!((routed || is_remote) && (row->flags & SYS_DEVICE_OP_LOCAL)), ERR_DEV_LOCAL_OP, .op = op);
  if (is_remote) {
    *remote = *(const sys_device_remote_t*)sys_device_slot_instance(id);
    return NULL;
  }
  if (row->flags & SYS_DEVICE_OP_NOLOCK) {
    ERR_TRY(sys_device_slot_enter(id));
    *later = (unlocked_t){.fn = fn, .instance = sys_device_slot_instance(id)};
    return NULL;
  }
  return fn(sys_device_slot_instance(id), in, out);
}

/* What happened to a device: the change of its slot across one op (taken under its lock), or a RESET of its type that
 * worked. 0 when nothing did. A remote device's RESET is its board's to tell (its events come over). */
static uint32_t change_of(uint8_t id, uint16_t op, bool was_installed, bool was_suspended, err_h err) {
  const bool installed = sys_device_slot_is_installed(id);
  if (installed != was_installed) {
    return installed ? SYS_DEVICE_EV_CREATED : SYS_DEVICE_EV_REMOVED;
  }
  const bool suspended = sys_device_slot_is_suspended(id);
  if (installed && suspended != was_suspended) {
    return suspended ? SYS_DEVICE_EV_SUSPENDED : SYS_DEVICE_EV_RESUMED;
  }
  if (installed && op == CONTRACT_SYS_DEVICE_OP_RESET && err == NULL && !sys_device_slot_is_remote(id)) {
    return SYS_DEVICE_EV_RESET;
  }
  return 0;
}

/* Published after the lock is let go, inside the call: it goes into the task's hold with the events of the op and
 * reaches the listeners when the task's outermost call is over, so a listener may call the registry; its failure is
 * not the op's. `source` is the device, or SYS_DEVICE_ID_NONE for a sweep; `gen` is the slot's generation, taken under
 * the lock (CREATED: the new device's; REMOVED: the removed one's, which the free slot keeps). */
static void publish_change(uint8_t source, uint32_t change, uint8_t gen) {
  switch (change) {
    case SYS_DEVICE_EV_CREATED:
      err_report(SYS_EVENT_PUBLISH(SYS_DEVICE_EV_CREATED, source, 0, .gen = gen));
      break;
    case SYS_DEVICE_EV_REMOVED:
      err_report(SYS_EVENT_PUBLISH(SYS_DEVICE_EV_REMOVED, source, 0, .gen = gen));
      break;
    case SYS_DEVICE_EV_SUSPENDED:
      err_report(SYS_EVENT_PUBLISH(SYS_DEVICE_EV_SUSPENDED, source, 0));
      break;
    case SYS_DEVICE_EV_RESUMED:
      err_report(SYS_EVENT_PUBLISH(SYS_DEVICE_EV_RESUMED, source, 0));
      break;
    case SYS_DEVICE_EV_RESET:
      err_report(SYS_EVENT_PUBLISH(SYS_DEVICE_EV_RESET, source, 0));
      break;
    default:
      break;  // 0: nothing happened
  }
}

/* The device's stage of a call. A remote device's call goes to the hook after the lock: it waits for another board,
 * and that board's events come back on a task that may call this device. A NOLOCK op runs after the lock too, counted
 * in flight. What the op changed is published after the lock. A step takes the lock only when it is free and runs only
 * on the device it is for; else it is skipped, with no error. */
static err_h call_device(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out, const how_t* how) {
  sys_device_remote_t remote = {0};
  unlocked_t later = {0};
  if (how->step) {
    if (!sys_device_slot_try_lock(id, op)) {
      return NULL;  // busy: the next step comes in its own time
    }
  } else {
    ERR_TRY(sys_device_slot_lock(id, op));
  }
  const bool stale = how->gen != 0 && (!sys_device_slot_is_installed(id) || sys_device_slot_gen(id) != how->gen);
  if (stale || (how->step && (sys_device_slot_is_suspended(id) || gated()))) {
    sys_device_slot_unlock(id);
    return NULL;  // gone, another device under this ID, or a step on a suspended/gated slot
  }
  const bool was_installed = sys_device_slot_is_installed(id);
  const bool was_suspended = sys_device_slot_is_suspended(id);
  const err_h err = run_locked(id, dir, op, in, out, how->routed, &remote, &later);
  const uint32_t change = change_of(id, op, was_installed, was_suspended, err);
  const uint8_t gen = sys_device_slot_gen(id);
  sys_device_slot_unlock(id);
  // A sweep tells of SUSPENDED / RESUMED / RESET once, for all its devices; CREATED and REMOVED are always a device's.
  if (!how->sweep || change == SYS_DEVICE_EV_CREATED || change == SYS_DEVICE_EV_REMOVED) {
    publish_change(id, change, gen);
  }
  if (later.fn != NULL) {
    const err_h op_err = later.fn(later.instance, in, out);
    sys_device_slot_leave(id);
    return op_err;
  }
  if (err != NULL || remote.flags == 0) {
    return err;  // done: a local device, a registry op, or refused
  }
  return sys_device_remote_send(id, &remote, dir, op, in, out);
}

/* One call (outermost or nested): ensures a non-NULL answer buffer, links this call's frame onto `s_frame`, runs
 * any contract interceptor around the device call, and wraps any error in ERR_DEV_OP + ERR_DEV_ID. */
static err_h call_exec(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out, const how_t* how) {
  cmd_out_t none = {0};  // no answer wanted: a handler still gets a buffer, one that holds nothing
  cmd_out_t* answer = out != NULL ? out : &none;

  const call_frame_t frame = {.up = s_frame, .id = id < SYS_DEVICE_COUNT ? id : SYS_DEVICE_ID_NONE};
  s_frame = &frame;

  /* Both interceptor stages run without any lock of the registry: the first may clamp/transform args into `buf` (or
   * refuse the call), the second is told how a call it let through ended so it can return any reserved budget. */
  const interceptor_t* interceptor = find_interceptor(op);
  uint8_t buf[INTERCEPT_MAX];
  err_h err = NULL;
  if (interceptor != NULL) {
    cmd_out_t passed = {.data = buf, .cap = sizeof(buf), .len = 0};
    err = interceptor->fn(id, dir, op, in, &passed);
    if (err == NULL && passed.len > 0) {
      in = (cmd_in_t){.data = buf, .len = passed.len};
    }
  }
  if (err == NULL) {
    err = call_device(id, dir, op, in, answer, how);
    if (interceptor != NULL && interceptor->done != NULL) {
      interceptor->done(id, dir, op, in, err);
    }
  }

  s_frame = frame.up;
  if (err != NULL) {
    err = ERR_WRAP(err, ERR_DEV_OP, .op = op, .dir = dir);
    err = ERR_WRAP(err, ERR_DEV_ID, .id = id);
  }
  return err;
}

/* The task's outermost call: it holds no lock of a slot before it and none after it. The events published by the ops
 * under it (and the registry's own) are held on this frame and delivered when the last lock is gone, so an inline
 * listener runs in this task and may take the lock of any device, the publisher's and the caller's included. Not
 * inlined: only this frame pays for the room. */
static __attribute__((noinline)) err_h call_outermost(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out,
                                                      const how_t* how) {
  uint32_t room[CONFIG_SYS_DEVICE_EVENT_HOLD_BYTES / 4];
  sys_event_hold_t hold;
  sys_event_hold(&hold, room, sizeof(room));
  const err_h err = call_exec(id, dir, op, in, out, how);
  sys_event_release(&hold);  // also when the op failed
  return err;
}

static inline err_h call_dispatch(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out, const how_t* how) {
  return s_frame == NULL ? call_outermost(id, dir, op, in, out, how) : call_exec(id, dir, op, in, out, how);
}

err_h sys_device_call_routed(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out) {
  const how_t how = {.routed = true};
  return call_dispatch(id, dir, op, in, out, &how);
}

err_h sys_device_step(uint8_t id, uint8_t gen, uint16_t op) {
  ERR_CHECK(id < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = id);
  ERR_CHECK(gen != 0 && (op >> 8) == SYS_DEVICE_CONTRACT_OWN, ERR_DEV_BAD_ARG);  // own ops: no interceptor sees them
  const how_t how = {.step = true, .gen = gen};
  return call_dispatch(id, CMD_DIR_WRITE, op, (cmd_in_t){.data = NULL, .len = 0}, NULL, &how);
}

err_h sys_device_deliver(uint8_t id, uint8_t gen, uint16_t op, const void* args, uint32_t args_len) {
  ERR_CHECK(id < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = id);
  ERR_CHECK(gen != 0 && (op >> 8) != MODULE_ID_SYS_DEVICE, ERR_DEV_BAD_ARG);
  const how_t how = {.gen = gen};
  return call_dispatch(id, CMD_DIR_WRITE, op, (cmd_in_t){.data = args, .len = args_len}, NULL, &how);
}

/* ----------------------------------------------------------------------------
 * Public calls (sys_device.h)
 * ------------------------------------------------------------------------- */

err_h sys_device_call(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out) {
  const how_t how = {0};
  return call_dispatch(id, dir, op, in, out, &how);
}

err_h sys_device_write(uint8_t id, uint16_t op, const void* args, uint32_t args_len) {
  return sys_device_call(id, CMD_DIR_WRITE, op, (cmd_in_t){.data = args, .len = args_len}, NULL);
}

err_h sys_device_create_system(uint8_t id, uint16_t type_id, const void* cfg, uint32_t cfg_len) {
  uint8_t frame[SYS_DEVICE_CREATE_HDR + SYS_DEVICE_CREATE_CFG_MAX];
  ERR_CHECK(cfg_len <= SYS_DEVICE_CREATE_CFG_MAX, ERR_CMD_BAD_LEN, .got = (uint16_t)cfg_len,
            .need = (uint16_t)SYS_DEVICE_CREATE_CFG_MAX);
  memcpy(frame, &type_id, sizeof(type_id));
  frame[sizeof(type_id)] = SYS_DEVICE_CREATE_SYSTEM;
  if (cfg_len > 0u) {
    memcpy(frame + SYS_DEVICE_CREATE_HDR, cfg, cfg_len);
  }
  return sys_device_write(id, CONTRACT_SYS_DEVICE_OP_CREATE, frame, SYS_DEVICE_CREATE_HDR + cfg_len);
}

err_h sys_device_read(uint8_t id, uint16_t op, const void* args, uint32_t args_len, void* out, uint32_t out_size) {
  cmd_out_t answer = {.data = out, .cap = out_size, .len = 0};
  ERR_TRY(sys_device_call(id, CMD_DIR_READ, op, (cmd_in_t){.data = args, .len = args_len}, &answer));
  ERR_CHECK(answer.len == out_size, ERR_CMD_BAD_LEN, .got = (uint16_t)answer.len, .need = (uint16_t)out_size);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * A sweep: one op on every device
 * ------------------------------------------------------------------------- */

/* The devices in the order of the declared uses, a device before the ones it uses: every round takes the devices no
 * device left uses. A use can't close into a cycle (ERR_DEV_CYCLE), so a round always takes one; should the masks be
 * read while they change, the rest is taken as it is and the sweep still ends. A snapshot: a device created or removed
 * meanwhile is another call's. */
static uint32_t sweep_order(uint8_t* order) {
  uint64_t left = sys_device_slot_installed();
  uint32_t n = 0;
  while (left != 0) {
    uint64_t round = 0;
    for (uint64_t m = left; m != 0; m &= m - 1) {
      const uint8_t id = (uint8_t)__builtin_ctzll(m);
      if ((sys_device_slot_used_by(id) & left) == 0) {
        round |= 1ULL << id;
      }
    }
    if (round == 0) {
      round = left;
    }
    for (uint64_t m = round; m != 0; m &= m - 1) {
      order[n++] = (uint8_t)__builtin_ctzll(m);
    }
    left &= ~round;
  }
  return n;
}

// Whether the sweep of `op` leaves device `id` out: REMOVE a board device, RESET a device whose type has none.
static bool sweep_skips(uint8_t id, uint16_t op) {
  if (op == CONTRACT_SYS_DEVICE_OP_REMOVE) {
    return sys_device_slot_is_system(id);
  }
  if (op == CONTRACT_SYS_DEVICE_OP_RESET) {
    return sys_device_op_side(sys_device_type_op(id, op), CMD_DIR_WRITE) == NULL;
  }
  return false;
}

static uint32_t sweep_event(uint16_t op) {
  switch (op) {
    case CONTRACT_SYS_DEVICE_OP_SUSPEND:
      return SYS_DEVICE_EV_SUSPENDED;
    case CONTRACT_SYS_DEVICE_OP_RESUME:
      return SYS_DEVICE_EV_RESUMED;
    case CONTRACT_SYS_DEVICE_OP_RESET:
      return SYS_DEVICE_EV_RESET;
    default:
      return 0;  // REMOVE: each device's REMOVED
  }
}

/* The sweep holds its events on this frame (unless the task is in an op already, whose outermost call holds them) and
 * sends them out after the last device, the sweep's own event last. */
err_h sys_device_sweep(uint16_t op) {
  ERR_CHECK(op == CONTRACT_SYS_DEVICE_OP_SUSPEND || op == CONTRACT_SYS_DEVICE_OP_RESUME || op == CONTRACT_SYS_DEVICE_OP_RESET ||
                op == CONTRACT_SYS_DEVICE_OP_REMOVE,
            ERR_DEV_UNKNOWN_OP, .op = op);
  uint8_t order[SYS_DEVICE_COUNT];
  uint32_t room[CONFIG_SYS_DEVICE_EVENT_HOLD_BYTES / 4];
  sys_event_hold_t hold;
  sys_event_hold(&hold, room, sizeof(room));
  const bool safe_state = op == CONTRACT_SYS_DEVICE_OP_SUSPEND;
  if (safe_state) {
    safe_gate(true);  // before the first device: no write from outside starts an output the sweep has not reached
  }

  const how_t how = {.sweep = true};
  const uint32_t count = sweep_order(order);
  err_h first = NULL;
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t id = op == CONTRACT_SYS_DEVICE_OP_RESUME ? order[count - 1u - i] : order[i];  // RESUME: the used ones first
    if (sweep_skips(id, op)) {
      continue;
    }
    const err_h err = call_exec(id, CMD_DIR_WRITE, op, (cmd_in_t){0}, NULL, &how);
    if (first == NULL) {
      first = err;
    } else {
      err_report(err);
    }
  }

  if (safe_state) {
    safe_gate(false);
  }
  publish_change(SYS_DEVICE_ID_NONE, sweep_event(op), 0);
  sys_event_release(&hold);
  return first;
}
