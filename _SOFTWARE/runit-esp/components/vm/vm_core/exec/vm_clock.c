#include "vm_clock.h"
#include "vm_err.h"


uint64_t g_vm_pass_ms;

static struct {
  uint64_t held_us;        // total time spent holding, subtracted from the real clock
  uint64_t hold_since_us;  // start of the hold in progress, 0 = not holding
  uint64_t prog_us;        // program time
  uint64_t run_prev_us;    // running real time (clock - held) at the previous pass
  bool prog_clock_on;      // false until the first pass after a reset
} s_clock;

/* The slow-motion settings start non-zero, so they stay out of s_clock (which stays in .bss). */
static struct {
  volatile uint16_t slowdown;  // slow-motion factor, 1 = normal
  volatile uint16_t step_ms;   // program time a stepped pass adds
} s_clock_preset = {.slowdown = 1, .step_ms = 10};

void vm_clock_reset(void) {
  g_vm_pass_ms = 0;
  s_clock.held_us = 0;
  s_clock.hold_since_us = 0;
  s_clock.prog_clock_on = false;
}

void vm_clock_hold_begin(void) {
  if (!s_clock.hold_since_us) s_clock.hold_since_us = vm_clock_us();
}

void vm_clock_hold_end(void) {
  if (s_clock.hold_since_us) {
    s_clock.held_us += vm_clock_us() - s_clock.hold_since_us;
    s_clock.hold_since_us = 0;
  }
}

void vm_clock_hold_add(uint64_t us) {
  s_clock.held_us += us;
}

void vm_clock_pass_start(uint64_t now_us, bool stepped) {
  const uint64_t run_now = now_us - s_clock.held_us;
  if (!s_clock.prog_clock_on) {  // the first pass starts program time at the real clock, so it is never 0
    s_clock.prog_us = run_now;
    s_clock.prog_clock_on = true;
  } else if (stepped) {  // the time between two requests is held, the pass counts as one step
    s_clock.prog_us += (uint64_t)s_clock_preset.step_ms * 1000u;
  } else {
    s_clock.prog_us += (run_now - s_clock.run_prev_us) / s_clock_preset.slowdown;
  }
  s_clock.run_prev_us = run_now;
  g_vm_pass_ms = s_clock.prog_us / 1000u;
}

err_h vm_clock_set_slowdown(uint16_t factor) {
  ERR_CHECK(factor >= 1 && factor <= VM_CLOCK_SLOWDOWN_MAX, ERR_VM_BAD_ARG);
  s_clock_preset.slowdown = factor;
  return NULL;
}

uint16_t vm_clock_slowdown(void) {
  return s_clock_preset.slowdown;
}

err_h vm_clock_set_step_ms(uint16_t ms) {
  ERR_CHECK(ms <= VM_CLOCK_STEP_MS_MAX, ERR_VM_BAD_ARG);
  s_clock_preset.step_ms = ms;
  return NULL;
}

uint16_t vm_clock_step_ms(void) {
  return s_clock_preset.step_ms;
}
