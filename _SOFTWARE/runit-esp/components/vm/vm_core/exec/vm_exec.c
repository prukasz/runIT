#include "vm_exec.h"
#include "esp_log.h"
#include "vm_err.h"
#include "utils.h"
#include "vm_block.h"
#include "vm_event.h"
#include "vm_override.h"
#include "vm_retain.h"
#include "sys_actions.h"
#include "vm_stats.h"
#include "vm_store.h"


static const char* TAG = "vm_exec";

err_h vm_exec_check_block_type(uint16_t blk_id, uint8_t block_type) {
  if (!vm_block_fn_for(block_type)) {
    return ERR_NEW(ERR_VM_BLK_UNKNOWN_TYPE, .blk_id = blk_id, .block_type = block_type);
  }
  return NULL;
}


/* The task runs on core CONFIG_VM_EXEC_TASK_CORE so a tight pass cannot starve BLE on core 0.
   Priority sits above the idle task and below the drivers and the BLE host --
   a late pass is a late pass, a late radio is a dropped connection. */

U_TASK_DEFINE(vm_exec_task_h, 6144);

static struct {
  volatile vm_run_mode_e mode;
  uint8_t span_depth;
  vm_block_h current_block;
  vm_span_t child_bounds;
  bool program_locked;
  volatile bool pass_active;
  volatile bool cancel;
  bool stop_requested;
  TaskHandle_t pass_task;
  vm_exec_fault_status_t fault;
  bool waiting;
  vm_run_mode_e resume;
  void (*sample_hook)(void);  // telemetry sampling, called at the end of every completed pass
} s_exec;

/* Starts non-zero, so it stays out of s_exec (which stays in .bss). */
static struct {
  uint16_t next_block;
  vm_run_mode_e selected;
  volatile uint16_t wd_quiet;  // set by the watchdog sampler, cleared by the pass
} s_exec_preset = {.next_block = UINT16_MAX, .selected = VM_RUN_RUNNING, .wd_quiet = VM_ID_NONE};

static portMUX_TYPE s_program_mux = portMUX_INITIALIZER_UNLOCKED;

static void request_stop_locked(void) {
  s_exec.stop_requested = true;
  s_exec.cancel = true;
  if (!s_exec.pass_active && !s_exec.program_locked) {
    s_exec.mode = VM_RUN_STOPPED;
    s_exec.waiting = false;
    s_exec_preset.next_block = UINT16_MAX;
    s_exec.stop_requested = false;
    s_exec.cancel = false;
  }
}

static bool caller_owns_active_pass(void) {
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_program_mux);
  bool owns = s_exec.pass_active && s_exec.pass_task == caller;
  portEXIT_CRITICAL(&s_program_mux);
  return owns;
}

static void wait_for_quiescence(void) {
  for (;;) {
    portENTER_CRITICAL(&s_program_mux);
    bool quiescent = !s_exec.pass_active && !s_exec.waiting;
    portEXIT_CRITICAL(&s_program_mux);
    if (quiescent) return;
    vTaskDelay(1);
  }
}

/* Cancel at a block boundary and wait for the stack to release all handles
   before the loader can replace the program. The caller/task check covers
   both the supervisor and a task executing vm_exec_pass() directly. */
err_h vm_exec_program_lock(vm_run_mode_e* out_previous) {
  ERR_CHECK(out_previous != NULL, ERR_VM_BAD_ARG);
  if (caller_owns_active_pass()) {
    return ERR_NEW(ERR_VM_EXEC_SELF_BARRIER, .operation = 1);
  }

  for (;;) {
    portENTER_CRITICAL(&s_program_mux);
    if (!s_exec.program_locked) {
      s_exec.program_locked = true;
      *out_previous = s_exec.mode;
      s_exec.cancel = true;
      portEXIT_CRITICAL(&s_program_mux);
      break;
    }
    portEXIT_CRITICAL(&s_program_mux);
    vTaskDelay(1);
  }

  wait_for_quiescence();
  portENTER_CRITICAL(&s_program_mux);
  s_exec.mode = VM_RUN_STOPPED;
  portEXIT_CRITICAL(&s_program_mux);
  return NULL;
}

void vm_exec_program_unlock(vm_run_mode_e mode) {
  portENTER_CRITICAL(&s_program_mux);
  if (s_exec.stop_requested || s_exec.fault.latched) {
    s_exec.mode = VM_RUN_STOPPED;
    s_exec.stop_requested = false;
    s_exec.cancel = false;
  } else {
    s_exec.mode = mode;
    s_exec.cancel = false;
  }
  s_exec.program_locked = false;
  portEXIT_CRITICAL(&s_program_mux);
}

/* Block watchdog: published (seq << 16) | block_id sampled periodically by esp_timer on core 0. A block reported
   hung stays quiet (s_exec_preset.wd_quiet) until one of its calls finishes within the period, or the program starts again:
   a block slow on every pass is one report, not one per pass. */
static struct {
  volatile uint32_t word;  // (seq << 16) | block index; 0 = idle
  uint16_t seq;
  esp_timer_handle_t timer;
  uint32_t last;
  bool reported;        // sampler-owned
  volatile bool rearm;  // a start asked for it: the pass clears the episodes (vm_block_report_error)
} s_wd;

static __always_inline void wd_enter(uint16_t blk_id) {
  if (++s_wd.seq == 0) ++s_wd.seq;  // block 0 must never publish the idle sentinel
  s_wd.word = ((uint32_t)s_wd.seq << 16) | blk_id;
}

static __always_inline void wd_leave(void) {
  s_wd.word = 0;
}

static void wd_sample(void* arg) {
  (void)arg;
  uint32_t cur = s_wd.word;
  if (cur != s_wd.last) s_wd.reported = false;
  if (cur != 0 && cur == s_wd.last && !s_wd.reported) {
    s_wd.reported = true;
    const uint16_t blk = (uint16_t)(cur & 0xFFFFu);
    if (blk != s_exec_preset.wd_quiet) {
      s_exec_preset.wd_quiet = blk;
      VM_RAISE(ERR_VM_EXEC_BLOCK_HUNG, .block_idx = blk, .ms = CONFIG_VM_EXEC_BLOCK_WD_MS);
    }
  }
  s_wd.last = cur;
}

void vm_exec_report(err_h chain) {
  const uint32_t timed = s_wd.word;
  wd_leave();
  err_report(chain);
  if (timed) wd_enter((uint16_t)timed);
}

// Start of a pass after a start command: every block reports its next failure, and a quiet block its next stall.
static void rearm(void) {
  s_wd.rearm = false;
  s_exec_preset.wd_quiet = VM_ID_NONE;
  for (uint16_t i = 0; i < g_vm_store.reg[VM_REG_BLK].count; i++) {
    vm_block_h b = vm_block_get_by_id(i);
    if (b) b->cfg.rt &= (uint8_t)~VM_BLK_RT_REPORTED;
  }
}

void vm_exec_set_sample_hook(void (*hook)(void)) {
  s_exec.sample_hook = hook;
}

// Records are queued on the router, never run on this task (vm_exec.h).
err_h vm_exec_request_action(uint32_t id) {
  return sys_actions_request(id, false);
}

/* ==========================================================================
   The pass
   ========================================================================== */

/* Overflow reporting stays outside block dispatch. */
static void report_event_overflow(void) {
  err_h e = vm_event_take_overflow();
  if (unlikely(e != NULL)) err_report(e);
}

bool vm_exec_cancelled(void) {
  return s_exec.cancel;
}

/* Keep the C stack, including FOR iterators, while waiting. A parent watchdog
   must be idle here too: waiting for the operator is not a hung block. */
static bool block_gate(uint16_t id) {
  // Fast path: in normal continuous run or single-pass step, do not acquire spinlock
  if (likely((s_exec.mode == VM_RUN_RUNNING || s_exec.mode == VM_RUN_STEP) && !s_exec.cancel)) {
    return true;
  }

  wd_leave();
  const uint64_t wait_t0 = vm_clock_us();
  bool sampled = false;
  for (;;) {
    portENTER_CRITICAL(&s_program_mux);
    bool hold = s_exec.mode == VM_RUN_FROZEN || s_exec.mode == VM_RUN_SCAN || s_exec.mode == VM_RUN_BLOCK;
    if (s_exec.cancel || !hold) {
      bool run = !s_exec.cancel;
      if (run && s_exec.mode == VM_RUN_BLOCK_STEP) s_exec.mode = VM_RUN_BLOCK;
      s_exec.waiting = false;
      s_exec_preset.next_block = UINT16_MAX;
      portEXIT_CRITICAL(&s_program_mux);
      vm_clock_hold_add(vm_clock_us() - wait_t0);  // waiting for the operator is not program time
      return run;
    }
    s_exec.waiting = true;
    s_exec_preset.next_block = id;
    portEXIT_CRITICAL(&s_program_mux);
    /* Block mode: the operator is stepping, so what the block before this one did is sent now, once per stop, rather than at the end
       of the pass (the pass sample sends only what changed since). This task is the pass: sampling here does not run beside it. */
    if (!sampled && s_exec_preset.selected == VM_RUN_BLOCK && s_exec.sample_hook) {
      sampled = true;
      s_exec.sample_hook();
    }
    vTaskDelay(1);
  }
}

// Dispatch block function and enforce cfg.on_error
static void run_block(vm_block_h b, vm_block_fn fn) {
  // per-call bits only; VM_BLK_RT_SPAN_BAD and anything sticky survives
  b->cfg.rt &= (uint8_t)~VM_BLK_RT_PER_CALL;

#if VM_STATS_PROFILE
  const uint64_t block_t0 = vm_clock_us();
#endif
  fn(b);
#if VM_STATS_PROFILE
  vm_stats_block(b->cfg.block_idx, b->cfg.block_type, (uint32_t)(vm_clock_us() - block_t0));
#endif

  /* cfg.on_error:
     STOP publishes a false ENO, so everything downstream self-skips. CONTINUE
     leaves the flow below untouched, and the error is already on its way to
     the handler either way.

     This has to be out here rather than in the block, because by the time a
     body knows it failed it may already have published and returned. The
     outputs are *not* retracted: whatever the block wrote is what it computed,
     and the flow being stopped is what keeps anything from acting on it. */
  if (unlikely(vm_block_failed(b))) {
    if (b->cfg.on_error == VM_BLK_ERR_STOP) vm_block_set_eno(b, false);
  } else {
    b->cfg.rt &= (uint8_t)~VM_BLK_RT_REPORTED;  // a call that passed ends the episode
  }
}

void vm_exec_run_range(uint16_t start, uint16_t end) {
  // Reject self-recursion and range escapes before any nested block can act
  if (s_exec.current_block &&
      (start != s_exec.child_bounds.start || end <= start || end > s_exec.child_bounds.end)) {
    vm_block_mark_failed(s_exec.current_block);
    if (!(s_exec.current_block->cfg.rt & VM_BLK_RT_SPAN_BAD)) {
      s_exec.current_block->cfg.rt |= VM_BLK_RT_SPAN_BAD;
      VM_RAISE(ERR_VM_EXEC_BAD_SPAN, .block_idx = s_exec.current_block->cfg.block_idx, .start = start, .end = end);
    }
    return;
  }
  if (unlikely(s_exec.span_depth >= CONFIG_VM_EXEC_MAX_SPAN_DEPTH)) {
    if (s_exec.current_block) vm_block_mark_failed(s_exec.current_block);
    VM_RAISE(ERR_VM_EXEC_SPAN_DEPTH, .block_idx = start, .depth = CONFIG_VM_EXEC_MAX_SPAN_DEPTH);
    return;
  }
  s_exec.span_depth++;
  vm_block_h outer_block = s_exec.current_block;
  const vm_span_t outer_bounds = s_exec.child_bounds;
  const uint32_t outer_wd = s_wd.word;

  for (uint16_t i = start; i < end;) {
    if (vm_exec_cancelled()) break;
    vm_block_h b = vm_block_get_by_id(i);
    if (unlikely(!b)) {
      i++;
      continue;
    }

    /* The load rejects a type the table does not fill
       (vm_exec_check_block_type), so a miss here means the table in force
       changed under a loaded program -- which only a test can do. Skipping is
       the only safe answer: there is nothing to call. */
    vm_block_fn fn = vm_block_fn_for(b->cfg.block_type);
    if (unlikely(!fn)) {
      i++;
      continue;
    }

    if (!block_gate(i)) break;
    s_exec.current_block = b;
    s_exec.child_bounds = (vm_span_t){(uint16_t)(i + 1), end};
    const bool quiet = unlikely(s_exec_preset.wd_quiet == i);  // only a block reported hung is timed here
    const uint64_t call_t0 = quiet ? vm_clock_us() : 0;
    wd_enter(i);
    run_block(b, fn);
    wd_leave();
    if (quiet && vm_clock_us() - call_t0 < (uint64_t)CONFIG_VM_EXEC_BLOCK_WD_MS * 1000u) s_exec_preset.wd_quiet = VM_ID_NONE;

    uint16_t next = (uint16_t)(i + 1);

    // Span owner claimed the following range; advance walk past the span
    if (unlikely(b->cfg.rt & VM_BLK_RT_SPAN)) {
      const vm_span_t* sp = vm_block_get_span(b);
      if (likely(sp && sp->start == next && sp->end > sp->start && sp->end <= end)) {
        next = sp->end;
      } else if (!(b->cfg.rt & VM_BLK_RT_SPAN_BAD)) {
        b->cfg.rt |= VM_BLK_RT_SPAN_BAD;
        VM_RAISE(ERR_VM_EXEC_BAD_SPAN, .block_idx = b->cfg.block_idx, .start = sp ? sp->start : 0,
                    .end = sp ? sp->end : 0);
      }
    }

    i = next;
  }

  s_exec.span_depth--;
  s_exec.current_block = outer_block;
  s_exec.child_bounds = outer_bounds;
  /* Returning from a body is progress: resume timing the owner with a fresh
     token, rather than making repeated visits look like one stalled call. */
  if (outer_wd && !vm_exec_cancelled()) wd_enter((uint16_t)outer_wd);
}

void vm_exec_pass(void) {
  TaskHandle_t pass_task = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_program_mux);
  /* STOPPED parks the supervisor, but vm_exec_pass() is also the explicit
     synchronous/manual execution entry point used by bring-up and tests.
     A pending stop closes that exception until a new mode is selected. */
  bool stopped_holds_caller =
      s_exec.mode == VM_RUN_STOPPED &&
      (s_exec.stop_requested || s_exec.fault.latched || (vm_exec_task_h && pass_task == vm_exec_task_h));
  if (s_exec.fault.latched || s_exec.program_locked || s_exec.pass_active || stopped_holds_caller ||
      s_exec.mode == VM_RUN_FROZEN || s_exec.mode == VM_RUN_SCAN || s_exec.mode == VM_RUN_BLOCK) {
    portEXIT_CRITICAL(&s_program_mux);
    return;
  }
  s_exec.pass_active = true;
  s_exec.pass_task = pass_task;
  portEXIT_CRITICAL(&s_program_mux);
  const uint64_t t0 = vm_clock_us();
  vm_clock_pass_start(t0, s_exec_preset.selected != VM_RUN_RUNNING);
  vm_stats_pass_begin(t0);
  if (s_wd.rearm) rearm();

  /* Events, like the clock, are latched once for the whole pass: what this
     drain pulls out of the queue is what every block sees, start to finish. */
  vm_event_drain();
  vm_override_drain();

  report_event_overflow();
  const uint64_t t_events = vm_clock_us();
  vm_stats_phase(VM_PH_EVENTS, (uint32_t)(t_events - t0));
  vm_exec_run_range(0, g_vm_store.reg[VM_REG_BLK].count);
  const uint64_t t_blocks = vm_clock_us();
  vm_stats_phase(VM_PH_BLOCKS, (uint32_t)(t_blocks - t_events));

  // Order within pass: execute blocks -> sample subscriptions -> retained values
  bool completed = !vm_exec_cancelled();
  if (completed && s_exec.sample_hook) s_exec.sample_hook();
  const uint64_t t_sample = vm_clock_us();
  vm_stats_phase(VM_PH_SAMPLE, (uint32_t)(t_sample - t_blocks));
  if (completed) vm_retain_on_pass(t0 / 1000u);  // retention runs on the real clock, not program time
  const uint64_t t_end = vm_clock_us();
  vm_stats_phase(VM_PH_RETAIN, (uint32_t)(t_end - t_sample));

  portENTER_CRITICAL(&s_program_mux);
  if (!s_exec.program_locked && s_exec.mode == VM_RUN_STEP) {
    s_exec.mode = VM_RUN_FROZEN;
    s_exec.resume = VM_RUN_SCAN;
  }
  if (completed && s_exec.mode == VM_RUN_FROZEN && s_exec.resume == VM_RUN_STEP) s_exec.resume = VM_RUN_SCAN;
  if (completed && s_exec.mode == VM_RUN_FROZEN && s_exec.resume == VM_RUN_BLOCK_STEP) s_exec.resume = VM_RUN_BLOCK;
  // Empty programs also consume one NEXT. No dispatch means no gate did it.
  if (s_exec.mode == VM_RUN_BLOCK_STEP) s_exec.mode = VM_RUN_BLOCK;
  s_exec.pass_task = NULL;
  s_exec.pass_active = false;
  /* Publish STOPPED only after the pass no longer owns VM handles. Readers of
     the lock-free mode accessor can therefore treat STOPPED as quiescent. */
  if (!s_exec.program_locked && s_exec.stop_requested) {
    s_exec.mode = VM_RUN_STOPPED;
    s_exec.stop_requested = false;
    s_exec.cancel = false;
  }
  portEXIT_CRITICAL(&s_program_mux);

  vm_stats_pass_end(completed, (uint32_t)(t_end - t0));  // also the periodic log line
}

/* ==========================================================================
   The task
   ========================================================================== */

static void vm_exec_task(void* arg) {
  (void)arg;
  for (;;) {
    vm_run_mode_e mode = vm_exec_mode();
    if (mode == VM_RUN_STOPPED || mode == VM_RUN_FROZEN || mode == VM_RUN_SCAN || mode == VM_RUN_BLOCK) {
      vm_clock_hold_begin();  // not running passes: this time doesn't count for the program
      vTaskDelay(MSEC(10));
      continue;
    }
    vm_clock_hold_end();

    vm_exec_pass();

    // Yield 1 tick per pass to ensure core-1 idle task and drivers run (x the slow-motion factor)
    vTaskDelay(vm_clock_slowdown());
  }
}

err_h vm_exec_start(void) {
  if (vm_exec_task_h != NULL) return NULL;  // idempotent

  if (!s_wd.timer) {
    const esp_timer_create_args_t args = {
        .callback = wd_sample,
        .name = "vm_blk_wd",
    };
    if (esp_timer_create(&args, &s_wd.timer) != ESP_OK) {
      return ERR_NEW(ERR_VM_NO_MEM);
    }
    (void)esp_timer_start_periodic(s_wd.timer, (uint64_t)CONFIG_VM_EXEC_BLOCK_WD_MS * 1000u);
  }

  U_TASK_START_ON_CORE(vm_exec_task_h, vm_exec_task, NULL, CONFIG_VM_EXEC_TASK_PRIO, CONFIG_VM_EXEC_TASK_CORE);
  if (vm_exec_task_h == NULL) {
    return ERR_NEW(ERR_VM_NO_MEM);
  }

  ESP_LOGD(TAG, "supervisor started on core %d, %u block types in the palette", CONFIG_VM_EXEC_TASK_CORE, vm_block_types_count());
  return NULL;
}

void vm_exec_request_stop(void) {
  portENTER_CRITICAL(&s_program_mux);
  request_stop_locked();
  portEXIT_CRITICAL(&s_program_mux);
}

err_h vm_exec_stop(void) {
  vm_exec_request_stop();
  if (caller_owns_active_pass()) {
    return ERR_NEW(ERR_VM_EXEC_SELF_BARRIER, .operation = 0);
  }

  /* Hold the program barrier through the wait so a concurrent mode command
     cannot restart execution between quiescence and this function returning. */
  vm_run_mode_e previous;
  ERR_TRY(vm_exec_program_lock(&previous));
  vm_exec_program_unlock(VM_RUN_STOPPED);
  return NULL;
}

bool vm_exec_fault_latch(uint16_t device_id, uint32_t root_tag) {
  portENTER_CRITICAL(&s_program_mux);
  bool first = !s_exec.fault.latched;
  if (first) {
    s_exec.fault.latched = true;
    s_exec.fault.device_id = device_id;
    s_exec.fault.root_tag = root_tag;
    s_exec.fault.occurrences = 1;
  } else if (s_exec.fault.occurrences != UINT32_MAX) {
    s_exec.fault.occurrences++;
  }
  request_stop_locked();
  portEXIT_CRITICAL(&s_program_mux);
  return first;
}

vm_exec_fault_status_t vm_exec_fault_status(void) {
  portENTER_CRITICAL(&s_program_mux);
  vm_exec_fault_status_t status = s_exec.fault;
  portEXIT_CRITICAL(&s_program_mux);
  return status;
}

err_h vm_exec_fault_acknowledge(void) {
  portENTER_CRITICAL(&s_program_mux);
  bool valid = s_exec.mode == VM_RUN_STOPPED && !s_exec.pass_active && !s_exec.waiting && !s_exec.program_locked;
  if (valid) s_exec.fault = (vm_exec_fault_status_t){0};
  uint8_t mode = (uint8_t)s_exec.mode;
  portEXIT_CRITICAL(&s_program_mux);
  if (!valid) return ERR_NEW(ERR_VM_EXEC_CONTROL, .command = VM_EXEC_ACK_FAULT, .mode = mode);
  return NULL;
}

void vm_exec_set_mode(vm_run_mode_e mode) {
  if (mode == VM_RUN_STOPPED) {
    vm_exec_request_stop();
    return;
  }
  portENTER_CRITICAL(&s_program_mux);
  if (!s_exec.fault.latched && !s_exec.program_locked && !(s_exec.stop_requested && s_exec.pass_active) && mode <= VM_RUN_BLOCK_STEP) {
    if (mode == VM_RUN_FROZEN && s_exec.mode != VM_RUN_FROZEN) s_exec.resume = s_exec.mode;
    if (mode == VM_RUN_RUNNING) s_exec_preset.selected = VM_RUN_RUNNING;
    if (mode == VM_RUN_STEP || mode == VM_RUN_SCAN) s_exec_preset.selected = VM_RUN_SCAN;
    if (mode == VM_RUN_BLOCK || mode == VM_RUN_BLOCK_STEP) s_exec_preset.selected = VM_RUN_BLOCK;
    s_exec.stop_requested = false;
    s_exec.cancel = false;
    s_exec.mode = mode;
  }
  portEXIT_CRITICAL(&s_program_mux);
}

vm_run_mode_e vm_exec_mode(void) {
  return s_exec.mode;
}

vm_exec_status_t vm_exec_status(void) {
  portENTER_CRITICAL(&s_program_mux);
  vm_exec_status_t status = {s_exec.mode, s_exec_preset.next_block, s_exec.pass_active, s_exec.waiting, s_exec.stop_requested};
  portEXIT_CRITICAL(&s_program_mux);
  return status;
}

err_h vm_exec_control(vm_exec_command_e command) {
  if (command == VM_EXEC_ACK_FAULT) return vm_exec_fault_acknowledge();

  portENTER_CRITICAL(&s_program_mux);
  vm_exec_fault_status_t fault = s_exec.fault;
  portEXIT_CRITICAL(&s_program_mux);
  if (fault.latched) {
    return ERR_NEW(ERR_VM_EXEC_FAULT_LATCHED, .device_id = fault.device_id, .root_tag = fault.root_tag);
  }

  if (command == VM_EXEC_RESET_TO_START) {
    vm_run_mode_e previous;
    ERR_TRY(vm_exec_program_lock(&previous));
    s_exec.resume = s_exec_preset.selected;
    vm_exec_program_unlock(s_exec_preset.selected == VM_RUN_RUNNING ? VM_RUN_FROZEN : s_exec_preset.selected);
    return NULL;
  }
  /* First start after a load: stored retained values go in before the first
     pass. A failed restore is reported and the program starts anyway. */
  if (command == VM_EXEC_NORMAL_MODE || command == VM_EXEC_SCAN_MODE || command == VM_EXEC_BLOCK_MODE) {
    err_report(vm_retain_restore());
    s_wd.rearm = true;
  }

  portENTER_CRITICAL(&s_program_mux);
  bool valid = !s_exec.program_locked && !(s_exec.stop_requested && s_exec.pass_active);
  if (valid) switch (command) {
    case VM_EXEC_SCAN_MODE:
      s_exec.stop_requested = s_exec.cancel = false;
      s_exec_preset.selected = s_exec.mode = VM_RUN_SCAN;
      break;
    case VM_EXEC_BLOCK_MODE:
      s_exec.stop_requested = s_exec.cancel = false;
      s_exec_preset.selected = s_exec.mode = VM_RUN_BLOCK;
      break;
    case VM_EXEC_NORMAL_MODE:
      s_exec.stop_requested = s_exec.cancel = false;
      s_exec_preset.selected = s_exec.mode = VM_RUN_RUNNING;
      break;
    case VM_EXEC_ONCE:
      valid = s_exec_preset.selected == VM_RUN_SCAN && (s_exec.mode == VM_RUN_SCAN || s_exec.mode == VM_RUN_FROZEN);
      if (valid) s_exec.mode = VM_RUN_STEP;
      break;
    case VM_EXEC_NEXT:
      valid = s_exec_preset.selected == VM_RUN_BLOCK && s_exec.mode == VM_RUN_BLOCK && (!s_exec.pass_active || s_exec.waiting);
      if (valid) s_exec.mode = VM_RUN_BLOCK_STEP;
      break;
    case VM_EXEC_PAUSE:
      if (s_exec.mode != VM_RUN_FROZEN) { s_exec.resume = s_exec.mode; s_exec.mode = VM_RUN_FROZEN; }
      break;
    case VM_EXEC_RESUME:
      if (s_exec.mode == VM_RUN_FROZEN) s_exec.mode = s_exec.resume;
      break;
    default:
      valid = false;
      break;
  }
  uint8_t mode = (uint8_t)s_exec.mode;
  portEXIT_CRITICAL(&s_program_mux);
  if (!valid) return ERR_NEW(ERR_VM_EXEC_CONTROL, .command = (uint8_t)command, .mode = mode);
  return NULL;
}

void vm_exec_reset(void) {
  vm_event_reset();
  vm_override_reset();
  s_exec.span_depth = 0;
  s_exec.current_block = NULL;
  s_exec.child_bounds = (vm_span_t){0, 0};
  portENTER_CRITICAL(&s_program_mux);
  s_exec.waiting = false;
  s_exec_preset.next_block = UINT16_MAX;
  s_exec_preset.selected = VM_RUN_RUNNING;
  s_exec.resume = VM_RUN_STOPPED;
  s_exec.stop_requested = false;
  s_exec.pass_task = NULL;
  portEXIT_CRITICAL(&s_program_mux);
  wd_leave();  // sampler history stays owned by the timer task
  s_exec_preset.wd_quiet = VM_ID_NONE;  // an index of the old program
  vm_clock_reset();
  vm_exec_reset_stats();
}
