#pragma once
#include <stdbool.h>
#include <stdint.h>

/*
 * Pass statistics and the bring-up profile
 *
 * Everything the supervisor measures about itself, kept out of vm_exec.c: pass and cycle times
 * (vm_exec_get_perf) and, behind the two switches below, a log of where the time of a pass went.
 */

/* Bring-up switch: log how long a whole pass takes (events, every block, telemetry sample,
   retention), in us, once per window of this many ms. 0 = off. The line goes out on the logs
   stream, so the app's Errors & logs panel shows it. A pass held by pause / scan / block mode
   counts the wait too. */
#define VM_PASS_TIME_LOG_MS 5000

/* With the pass time log on: also time each phase of a pass and each block (a FOR counts its body),
   and log where the time went, with the slowest blocks. 0 = off. */
#define VM_PASS_PROFILE 1

#define VM_STATS_PROFILE (VM_PASS_TIME_LOG_MS && VM_PASS_PROFILE)

typedef struct vm_exec_perf_t {
  uint32_t pass_count;
  uint32_t last_pass_us;
  uint32_t min_pass_us;
  uint32_t max_pass_us;
  uint64_t total_pass_us;
  uint32_t last_cycle_us;
  uint32_t min_cycle_us;
  uint32_t max_cycle_us;
  uint64_t total_cycle_us;
} vm_exec_perf_t;

/** @brief Completed passes count since last stats reset. */
uint32_t vm_exec_pass_count(void);

/** @brief Wall duration of the last pass in microseconds. */
uint32_t vm_exec_last_pass_us(void);

/** @brief Retrieve comprehensive scan cycle performance metrics. */
vm_exec_perf_t vm_exec_get_perf(void);

/** @brief Reset pass metrics. */
void vm_exec_reset_stats(void);

/* ---- fed by the supervisor ---- */

typedef enum vm_phase_e { VM_PH_EVENTS, VM_PH_BLOCKS, VM_PH_SAMPLE, VM_PH_RETAIN, VM_PH_CLEAR, VM_PH_COUNT } vm_phase_e;

/** @brief Top of a pass: the time since the previous pass start is the cycle time. */
void vm_stats_pass_begin(uint64_t t0_us);

/** @brief End of a pass. @p dur_us only counts when @p completed. Also emits the periodic log line. */
void vm_stats_pass_end(bool completed, uint32_t dur_us);

#if VM_STATS_PROFILE
void vm_stats_phase(vm_phase_e phase, uint32_t us);
void vm_stats_block(uint16_t idx, uint8_t type, uint32_t us);
#else
static inline void vm_stats_phase(vm_phase_e phase, uint32_t us) { (void)phase; (void)us; }
#endif
