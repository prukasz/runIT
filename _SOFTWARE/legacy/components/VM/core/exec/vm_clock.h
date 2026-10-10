#pragma once
#include "esp_timer.h"
#include "sys_error.h"

/*
 * Program clock
 *
 * What the timing blocks (TIMER, PERIODIC) read. Program time is the real clock minus every
 * moment the VM spent not running passes (stopped, paused, scan / block stepping), divided
 * by the slow-motion factor. So a deliberate pause doesn't make a TIMER expire or a PERIODIC
 * report dropped ticks when it resumes, and a program slowed down x times keeps its timers x
 * times slower.
 *
 * Written only by the supervisor (vm_exec.c) and, for the factor, the 0x49 decoder; blocks only read.
 */

/** @brief Largest slow-motion factor (vm_clock_set_slowdown). */
#define VM_CLOCK_SLOWDOWN_MAX 1000  //@vm-constant @description Largest slow-motion factor of the program (1 = normal speed).

extern uint64_t g_vm_pass_ms;  // Latched pass timestamp in ms of program time

/** @brief Microseconds since boot, read live from timer (real time, not program time). */
static inline uint64_t vm_clock_us(void) {
  return (uint64_t)esp_timer_get_time();
}

/** @brief Pass timestamp in ms of program time, latched at top of current pass. Used by timing blocks. */
static inline uint64_t vm_now_ms(void) {
  return g_vm_pass_ms;
}

/** @brief A new program starts its clock afresh (the slow-motion factor stays). */
void vm_clock_reset(void);

/** @brief The VM stopped running passes (stopped, paused, stepping): from now on this time doesn't count. Idempotent. */
void vm_clock_hold_begin(void);

/** @brief Passes run again: close the hold in progress, if any. */
void vm_clock_hold_end(void);

/** @brief Time spent waiting inside a pass (block mode waiting for the operator) doesn't count either. */
void vm_clock_hold_add(uint64_t us);

/** @brief Top of a pass: advance program time and latch g_vm_pass_ms. @p now_us is vm_clock_us(). */
void vm_clock_pass_start(uint64_t now_us);

/**
 * @brief Slow-motion for debugging: the program runs @p factor times slower.
 *
 * The pause between passes grows @p factor times (a pass every ~10 ms becomes one every 10 ms x factor)
 * and program time runs @p factor times slower, so a 500 ms timer still spans the same number of
 * passes. 1 = normal speed. Applies at once, survives a program load, and is back to 1 at boot.
 * @return ERR_INVALID_VAL_UI32 when @p factor is outside 1..VM_CLOCK_SLOWDOWN_MAX.
 */
SE_MUST_USE err_h vm_clock_set_slowdown(uint16_t factor);

/** @brief Current slow-motion factor (1 = normal speed). */
uint16_t vm_clock_slowdown(void);
