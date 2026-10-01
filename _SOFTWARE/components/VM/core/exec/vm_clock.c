#include "vm_clock.h"

#define OWNER OWNER_VM_EXEC

uint64_t g_vm_pass_ms;

static uint64_t s_held_us;                // total time spent holding, subtracted from the real clock
static uint64_t s_hold_since_us;          // start of the hold in progress, 0 = not holding
static volatile uint16_t s_slowdown = 1;  // slow-motion factor, 1 = normal
static uint64_t s_prog_us;                // program time
static uint64_t s_run_prev_us;            // running real time (clock - held) at the previous pass
static bool s_prog_clock_on;              // false until the first pass after a reset

void vm_clock_reset(void) {
  g_vm_pass_ms = 0;
  s_held_us = 0;
  s_hold_since_us = 0;
  s_prog_clock_on = false;
}

void vm_clock_hold_begin(void) {
  if (!s_hold_since_us) s_hold_since_us = vm_clock_us();
}

void vm_clock_hold_end(void) {
  if (s_hold_since_us) {
    s_held_us += vm_clock_us() - s_hold_since_us;
    s_hold_since_us = 0;
  }
}

void vm_clock_hold_add(uint64_t us) {
  s_held_us += us;
}

void vm_clock_pass_start(uint64_t now_us) {
  const uint64_t run_now = now_us - s_held_us;
  if (!s_prog_clock_on) {  // the first pass starts program time at the real clock, so it is never 0
    s_prog_us = run_now;
    s_prog_clock_on = true;
  } else {
    s_prog_us += (run_now - s_run_prev_us) / s_slowdown;
  }
  s_run_prev_us = run_now;
  g_vm_pass_ms = s_prog_us / 1000u;
}

err_h vm_clock_set_slowdown(uint16_t factor) {
  SE_CHECK_IN_RANGE((uint32_t)factor, 1, VM_CLOCK_SLOWDOWN_MAX);
  s_slowdown = factor;
  return NULL;
}

uint16_t vm_clock_slowdown(void) {
  return s_slowdown;
}
